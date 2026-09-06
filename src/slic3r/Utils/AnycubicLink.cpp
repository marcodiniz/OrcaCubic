#include "AnycubicLink.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <thread>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/asio.hpp>
#include <boost/process.hpp>
#ifdef _WIN32
#include <boost/process/windows.hpp>
#endif
#include <nlohmann/json.hpp>

#include <openssl/md5.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/plugin/PythonInterpreter.hpp"
#include "Http.hpp"
#include "libslic3r/libslic3r.h"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

namespace fs = boost::filesystem;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

namespace Slic3r {

namespace {

std::mutex g_anycubic_bridge_mutex;
std::unique_ptr<boost::process::child> g_anycubic_bridge_process;
std::string g_anycubic_bridge_host;
std::string g_anycubic_bridge_token;

fs::path anycubic_bridge_token_path()
{
    return fs::path(data_dir()) / "anycubic-bridge.token";
}

std::string read_anycubic_bridge_token()
{
    std::ifstream stream(anycubic_bridge_token_path().string(), std::ios::binary);
    std::string token;
    std::getline(stream, token);
    boost::trim(token);
    return token;
}

std::string create_anycubic_bridge_token()
{
    const std::string token = boost::uuids::to_string(boost::uuids::random_generator()());
    std::ofstream stream(anycubic_bridge_token_path().string(), std::ios::binary | std::ios::trunc);
    stream << token;
    return stream ? token : std::string();
}

bool local_anycubic_bridge_matches(const std::string& host)
{
    bool matches = false;
    try {
        auto request = Http::get("http://127.0.0.1:18988/health");
        request.timeout_connect(1)
            .timeout_max(2)
            .on_complete([&](std::string body, unsigned status) {
                if (status != 200)
                    return;
                const auto payload = json::parse(body, nullptr, false, true);
                matches = !payload.is_discarded() && payload.value("ip", "") == host;
            })
            .perform_sync();
    } catch (...) {}
    return matches;
}

bool local_anycubic_bridge_running()
{
    bool running = false;
    try {
        auto request = Http::get("http://127.0.0.1:18988/health");
        request.timeout_connect(1)
            .timeout_max(2)
            .on_complete([&](std::string, unsigned status) { running = status == 200; })
            .perform_sync();
    } catch (...) {}
    return running;
}

bool ensure_anycubic_bridge(const std::string& host)
{
    if (host.empty())
        return false;

    std::lock_guard<std::mutex> lock(g_anycubic_bridge_mutex);
    if (g_anycubic_bridge_process && g_anycubic_bridge_process->running() && g_anycubic_bridge_host == host)
        return true;
    if (local_anycubic_bridge_matches(host)) {
        // A compatible bridge may have been started by another OrcaCubic process.
        // It is intentionally not adopted or terminated by this process.
        g_anycubic_bridge_token = read_anycubic_bridge_token();
        g_anycubic_bridge_host = host;
        return !g_anycubic_bridge_token.empty();
    }

    // Retarget a bridge owned by this process before treating the shared port as
    // an incompatible bridge owned by another OrcaCubic process.
    if (g_anycubic_bridge_process && g_anycubic_bridge_process->running()) {
        try {
            auto request = Http::post("http://127.0.0.1:18988/shutdown");
            request.header("X-OrcaCubic-Token", g_anycubic_bridge_token)
                .set_post_body(std::string("{}"))
                .timeout_connect(1)
                .timeout_max(2)
                .perform_sync();
        } catch (...) {}
        for (int attempt = 0; attempt < 20 && g_anycubic_bridge_process->running(); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (g_anycubic_bridge_process->running())
            g_anycubic_bridge_process->terminate();
        g_anycubic_bridge_process->wait();
        g_anycubic_bridge_process.reset();
        g_anycubic_bridge_host.clear();
        g_anycubic_bridge_token.clear();
    }

    if (local_anycubic_bridge_running()) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] Local bridge port 18988 is owned by a bridge configured for another printer";
        return false;
    }

    const fs::path script = fs::path(resources_dir()) / "scripts" / "anycubic_lan_daemon.py";
    const std::string python = PythonInterpreter::bundled_python_executable();
    if (python.empty() || !fs::exists(script)) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] Bundled LAN bridge runtime is unavailable";
        return false;
    }

    g_anycubic_bridge_token = create_anycubic_bridge_token();
    if (g_anycubic_bridge_token.empty()) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] Could not create the local bridge authentication token";
        return false;
    }

    try {
        g_anycubic_bridge_process = std::make_unique<boost::process::child>(
            python,
            script.string(),
            host,
            g_anycubic_bridge_token,
            boost::process::start_dir(script.parent_path().string()),
#ifdef _WIN32
            boost::process::windows::create_no_window,
#endif
            boost::process::std_out > boost::process::null,
            boost::process::std_err > boost::process::null);
        g_anycubic_bridge_host = host;
        BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Started bundled LAN bridge for configured printer host";
        return true;
    } catch (const std::exception& error) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] Failed to start bundled LAN bridge: " << error.what();
        g_anycubic_bridge_process.reset();
        g_anycubic_bridge_host.clear();
        g_anycubic_bridge_token.clear();
        return false;
    }
}

std::string normalize_anycubic_material(std::string material)
{
    boost::to_upper(material);
    material.erase(std::remove_if(material.begin(), material.end(), [](unsigned char ch) { return !std::isalnum(ch); }), material.end());
    return material;
}

} // namespace

std::vector<AnycubicAmsMappingEntry> build_anycubic_ams_mapping(
    const std::vector<AnycubicToolFilament>& tools,
    const std::vector<AnycubicMaterialSlot>& slots,
    const std::vector<int>& selected_slot_ids)
{
    if (tools.empty() || selected_slot_ids.size() != tools.size())
        return {};

    std::vector<AnycubicAmsMappingEntry> result;
    result.reserve(tools.size());
    for (size_t index = 0; index < tools.size(); ++index) {
        const auto slot_it = std::find_if(slots.begin(), slots.end(), [&](const AnycubicMaterialSlot& slot) {
            return slot.slot_id == selected_slot_ids[index];
        });
        if (slot_it == slots.end() || !slot_it->loaded || slot_it->slot_id < 1 || slot_it->slot_id > 4)
            return {};

        if (normalize_anycubic_material(slot_it->type) != normalize_anycubic_material(tools[index].type))
            return {};

        result.push_back({slot_it->slot_id - 1, tools[index].tool_id, slot_it->type, slot_it->color, tools[index].color});
    }
    return result;
}

AnycubicTaskSettings build_anycubic_task_settings(const AnycubicPrintSettings& settings)
{
    return {
        settings.auto_leveling ? 1 : 0,
        settings.vibration_compensation ? 1 : 0,
        settings.flow_calibration ? 1 : 0,
        settings.timelapse ? 1 : 0
    };
}

std::vector<AnycubicPrinterListEntry> build_anycubic_printer_list(
    const std::vector<AnycubicPrinterCandidate>& candidates,
    const std::string& active_host,
    const std::string& monitored_host)
{
    const auto normalize_host = [](std::string host) {
        boost::trim(host);
        if (boost::starts_with(host, "http://"))
            host.erase(0, 7);
        else if (boost::starts_with(host, "https://"))
            host.erase(0, 8);
        while (!host.empty() && host.back() == '/')
            host.pop_back();
        return host;
    };
    const std::string normalized_active_host = normalize_host(active_host);
    const std::string normalized_monitored_host = normalize_host(monitored_host);

    std::vector<AnycubicPrinterListEntry> result;
    for (const AnycubicPrinterCandidate& candidate : candidates) {
        if (candidate.host_type != "anycubic" || candidate.host.empty())
            continue;

        const std::string host = normalize_host(candidate.host);
        if (host.empty())
            continue;

        const bool active = host == normalized_active_host || (normalized_active_host.empty() && candidate.selected);
        const bool monitored = normalized_monitored_host.empty() ? active : host == normalized_monitored_host;
        result.push_back({candidate.preset_name, candidate.model_name, host, active, monitored, candidate.selected});
    }
    return result;
}

int find_anycubic_printer_candidate(
    const std::vector<AnycubicPrinterCandidate>& candidates,
    const std::string& preset_name)
{
    for (size_t index = 0; index < candidates.size(); ++index)
        if (candidates[index].preset_name == preset_name &&
            candidates[index].host_type == "anycubic" && !candidates[index].host.empty())
            return static_cast<int>(index);
    return -1;
}

std::optional<AnycubicPrinterSelection> choose_anycubic_printer(
    const std::vector<AnycubicPrinterCandidate>& candidates,
    const std::string& preset_name,
    bool make_active)
{
    const int index = find_anycubic_printer_candidate(candidates, preset_name);
    if (index < 0)
        return std::nullopt;

    std::string host = candidates[static_cast<size_t>(index)].host;
    boost::trim(host);
    if (boost::starts_with(host, "http://"))
        host.erase(0, 7);
    else if (boost::starts_with(host, "https://"))
        host.erase(0, 8);
    while (!host.empty() && host.back() == '/')
        host.pop_back();
    if (host.empty())
        return std::nullopt;
    return AnycubicPrinterSelection{preset_name, host, make_active};
}

namespace {

std::string safe_config_str(DynamicPrintConfig* config, const char* key)
{
    if (config == nullptr)
        return {};
    if (const auto* opt = config->option<ConfigOptionString>(key); opt != nullptr)
        return opt->value;
    return {};
}

std::string sanitize_anycubic_filename(const std::string& filename)
{
    std::string base = fs::path(filename).filename().string();
    if (base.empty())
        base = "print.gcode";
    return base;
}

std::string md5_hex(const std::string& input)
{
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
    char hex[33];
    for (int i = 0; i < 16; ++i) {
        snprintf(&hex[i * 2], 3, "%02x", digest[i]);
    }
    hex[32] = '\0';
    return std::string(hex);
}

bool base64_decode(const std::string& input, std::vector<unsigned char>& out)
{
    std::string padded = input;
    while (padded.size() % 4 != 0)
        padded.push_back('=');
    out.resize(boost::beast::detail::base64::decoded_size(padded.size()));
    auto res = boost::beast::detail::base64::decode(out.data(), padded.data(), padded.size());
    if (!res.second)
        return false;
    out.resize(res.first);
    return true;
}

bool aes128_cbc_decrypt(const std::vector<unsigned char>& ciphertext,
                        const std::string& key_str,
                        const std::string& iv_str,
                        std::string& plaintext)
{
    if (key_str.size() < 16 || iv_str.size() < 16 || ciphertext.empty())
        return false;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;

    bool ok = true;
    int len = 0;
    std::vector<unsigned char> plain(ciphertext.size() + 16);

    const auto* key = reinterpret_cast<const unsigned char*>(key_str.data());
    const auto* iv  = reinterpret_cast<const unsigned char*>(iv_str.data());

    if (EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, key, iv) != 1)
        ok = false;

    if (ok && EVP_DecryptUpdate(ctx, plain.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1)
        ok = false;
    int plain_len = len;

    if (ok && EVP_DecryptFinal_ex(ctx, plain.data() + len, &len) != 1)
        ok = false;
    plain_len += len;

    EVP_CIPHER_CTX_free(ctx);

    if (!ok)
        return false;

    plain.resize(plain_len);
    plaintext.assign(reinterpret_cast<const char*>(plain.data()), plain.size());
    return true;
}

std::string generate_nonce(size_t len = 6)
{
    static const char charset[] =
        "0123456789"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
    std::string s;
    s.reserve(len);
    for (size_t i = 0; i < len; ++i)
        s += charset[dist(gen)];
    return s;
}

std::string generate_uuid()
{
    boost::uuids::random_generator gen;
    return boost::uuids::to_string(gen());
}

std::string mqtt_enc_str(const std::string& str)
{
    std::string out;
    uint16_t len = static_cast<uint16_t>(str.size());
    out.push_back(static_cast<char>((len >> 8) & 0xFF));
    out.push_back(static_cast<char>(len & 0xFF));
    out.append(str);
    return out;
}

std::string mqtt_enc_len(size_t len)
{
    std::string out;
    do {
        uint8_t d = len % 128;
        len /= 128;
        if (len > 0)
            d |= 128;
        out.push_back(static_cast<char>(d));
    } while (len > 0);
    return out;
}

std::string mqtt_build_connect(const std::string& client_id, const std::string& username, const std::string& password)
{
    std::string proto = std::string("\x00\x04MQTT\x04", 6);
    char flags = static_cast<char>(0xc2); // Clean session + Username + Password
    std::string ka = std::string("\x00\x3c", 2); // 60s
    std::string payload = mqtt_enc_str(client_id) + mqtt_enc_str(username) + mqtt_enc_str(password);
    std::string body = proto + flags + ka + payload;
    return std::string("\x10", 1) + mqtt_enc_len(body.size()) + body;
}

std::string mqtt_build_subscribe(uint16_t pid, const std::string& topic)
{
    std::string p;
    p.push_back(static_cast<char>((pid >> 8) & 0xFF));
    p.push_back(static_cast<char>(pid & 0xFF));
    p.append(mqtt_enc_str(topic));
    p.push_back('\x00'); // QoS 0
    return std::string("\x82", 1) + mqtt_enc_len(p.size()) + p;
}

std::string mqtt_build_publish(const std::string& topic, const std::string& payload)
{
    std::string body = mqtt_enc_str(topic) + payload;
    return std::string("\x30", 1) + mqtt_enc_len(body.size()) + body;
}

class MqttTlsSession {
public:
    MqttTlsSession() = default;
    ~MqttTlsSession() { close(); }

    bool connect(const std::string& host, const std::string& port,
                 const std::string& username, const std::string& password,
                 const std::string& cert_pem, const std::string& key_pem,
                 const std::string& client_id, wxString& error_msg)
    {
        boost::system::error_code ec;
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        auto endpoints = resolver.resolve(host, port, ec);
        if (ec) {
            error_msg = GUI::from_u8((boost::format("Resolve failed: %1%") % ec.message()).str());
            return false;
        }

        m_socket = std::make_unique<tcp::socket>(ioc);
        net::connect(*m_socket, endpoints, ec);
        if (ec) {
            error_msg = GUI::from_u8((boost::format("TCP connect to %1%:%2% failed: %3%") % host % port % ec.message()).str());
            return false;
        }

        #if defined(_WIN32)
        DWORD timeout_ms = 5000;
        setsockopt(m_socket->native_handle(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        setsockopt(m_socket->native_handle(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        #else
        struct timeval tv{5, 0};
        setsockopt(m_socket->native_handle(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(m_socket->native_handle(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        #endif

        m_ctx = SSL_CTX_new(TLS_client_method());
        if (!m_ctx) {
            error_msg = _(L("Failed to create SSL context."));
            return false;
        }
        SSL_CTX_set_verify(m_ctx, SSL_VERIFY_NONE, nullptr);
        SSL_CTX_set_cipher_list(m_ctx, "DEFAULT:@SECLEVEL=0");

        BIO* bio_cert = BIO_new_mem_buf(cert_pem.data(), static_cast<int>(cert_pem.size()));
        X509* cert = PEM_read_bio_X509(bio_cert, nullptr, nullptr, nullptr);
        BIO_free(bio_cert);
        if (!cert) {
            error_msg = _(L("Failed to parse device certificate from credentials."));
            return false;
        }
        SSL_CTX_use_certificate(m_ctx, cert);
        X509_free(cert);

        BIO* bio_key = BIO_new_mem_buf(key_pem.data(), static_cast<int>(key_pem.size()));
        EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio_key, nullptr, nullptr, nullptr);
        BIO_free(bio_key);
        if (!pkey) {
            error_msg = _(L("Failed to parse device private key from credentials."));
            return false;
        }
        SSL_CTX_use_PrivateKey(m_ctx, pkey);
        EVP_PKEY_free(pkey);

        m_ssl = SSL_new(m_ctx);
        #if defined(_WIN32)
        SSL_set_fd(m_ssl, static_cast<int>(m_socket->native_handle()));
        #else
        SSL_set_fd(m_ssl, m_socket->native_handle());
        #endif

        if (SSL_connect(m_ssl) <= 0) {
            error_msg = _(L("TLS handshake failed with printer MQTT broker."));
            return false;
        }

        std::string conn_pkt = mqtt_build_connect(client_id, username, password);
        if (SSL_write(m_ssl, conn_pkt.data(), static_cast<int>(conn_pkt.size())) <= 0) {
            error_msg = _(L("Failed to send MQTT CONNECT."));
            return false;
        }

        char buf[64];
        int read_len = SSL_read(m_ssl, buf, sizeof(buf));
        if (read_len < 4 || static_cast<unsigned char>(buf[0]) != 0x20 || buf[3] != 0x00) {
            error_msg = GUI::from_u8((boost::format("MQTT CONNACK failed (rc=%1%)") % (read_len >= 4 ? static_cast<int>(buf[3]) : -1)).str());
            return false;
        }

        return true;
    }

    bool subscribe(uint16_t pid, const std::string& topic, wxString& error_msg)
    {
        if (!m_ssl) return false;
        std::string sub_pkt = mqtt_build_subscribe(pid, topic);
        if (SSL_write(m_ssl, sub_pkt.data(), static_cast<int>(sub_pkt.size())) <= 0) {
            error_msg = _(L("Failed to send MQTT SUBSCRIBE."));
            return false;
        }
        char buf[64];
        int read_len = SSL_read(m_ssl, buf, sizeof(buf));
        if (read_len < 3 || static_cast<unsigned char>(buf[0]) != 0x90) {
            error_msg = _(L("MQTT SUBACK failed."));
            return false;
        }
        return true;
    }

    bool publish(const std::string& topic, const std::string& payload, wxString& error_msg)
    {
        if (!m_ssl) return false;
        std::string pub_pkt = mqtt_build_publish(topic, payload);
        if (SSL_write(m_ssl, pub_pkt.data(), static_cast<int>(pub_pkt.size())) <= 0) {
            error_msg = _(L("Failed to send MQTT PUBLISH."));
            return false;
        }
        return true;
    }

    bool read_json_response(std::string& out_json_str, int timeout_sec = 4)
    {
        if (!m_ssl) return false;
        std::string accum;
        char buf[4096];
        auto start_time = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(timeout_sec)) {
            int bytes = SSL_read(m_ssl, buf, sizeof(buf));
            if (bytes > 0) {
                accum.append(buf, bytes);
                auto json_pos = accum.find("{\"");
                if (json_pos != std::string::npos) {
                    for (size_t end_pos = accum.size(); end_pos > json_pos; --end_pos) {
                        if (accum[end_pos - 1] == '}') {
                            std::string candidate = accum.substr(json_pos, end_pos - json_pos);
                            auto parsed = json::parse(candidate, nullptr, false, true);
                            if (!parsed.is_discarded() && parsed.is_object()) {
                                out_json_str = std::move(candidate);
                                return true;
                            }
                        }
                    }
                }
            } else {
                int ssl_err = SSL_get_error(m_ssl, bytes);
                if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue;
                }
                break;
            }
        }
        return false;
    }

    void close()
    {
        if (m_ssl) {
            SSL_shutdown(m_ssl);
            SSL_free(m_ssl);
            m_ssl = nullptr;
        }
        if (m_ctx) {
            SSL_CTX_free(m_ctx);
            m_ctx = nullptr;
        }
        if (m_socket) {
            try {
                boost::system::error_code ec;
                m_socket->shutdown(tcp::socket::shutdown_both, ec);
                m_socket->close(ec);
            } catch (...) {}
            // Do not delete or reset socket pointer synchronously while Windows IOCP may have pending operations
            auto leaked_sock = m_socket.release();
            (void)leaked_sock;
        }
    }

private:
    std::unique_ptr<tcp::socket> m_socket;
    SSL_CTX* m_ctx{nullptr};
    SSL* m_ssl{nullptr};
};

} // namespace

AnycubicLink::AnycubicLink(DynamicPrintConfig *config)
    : m_port("18910")
{
    std::string raw_host = safe_config_str(config, "print_host");
    boost::trim(raw_host);

    // Strip http:// or https://
    if (boost::starts_with(raw_host, "http://"))
        raw_host = raw_host.substr(7);
    else if (boost::starts_with(raw_host, "https://"))
        raw_host = raw_host.substr(8);

    // Split host:port if user entered custom port
    auto colon_pos = raw_host.find(':');
    if (colon_pos != std::string::npos) {
        m_host = raw_host.substr(0, colon_pos);
        m_port = raw_host.substr(colon_pos + 1);
    } else {
        m_host = raw_host;
    }

}

void reconcile_anycubic_lan_bridge(DynamicPrintConfig* config)
{
    if (config == nullptr) {
        shutdown_anycubic_lan_bridge();
        return;
    }
    const auto* host_type = config->option<ConfigOptionEnum<PrintHostType>>("host_type");
    if (host_type == nullptr || host_type->value != htAnycubic) {
        shutdown_anycubic_lan_bridge();
        return;
    }
    ensure_anycubic_bridge(Http::get_host_from_url(safe_config_str(config, "print_host")));
}

bool activate_anycubic_lan_bridge(const std::string& host)
{
    return ensure_anycubic_bridge(Http::get_host_from_url(host));
}

std::string anycubic_lan_bridge_host()
{
    std::lock_guard<std::mutex> lock(g_anycubic_bridge_mutex);
    return g_anycubic_bridge_host;
}

std::string anycubic_lan_bridge_token()
{
    std::lock_guard<std::mutex> lock(g_anycubic_bridge_mutex);
    if (g_anycubic_bridge_token.empty())
        g_anycubic_bridge_token = read_anycubic_bridge_token();
    return g_anycubic_bridge_token;
}

void shutdown_anycubic_lan_bridge()
{
    std::lock_guard<std::mutex> lock(g_anycubic_bridge_mutex);
    if (g_anycubic_bridge_process && g_anycubic_bridge_process->running()) {
        try {
            auto request = Http::post("http://127.0.0.1:18988/shutdown");
            request.header("X-OrcaCubic-Token", g_anycubic_bridge_token)
                .set_post_body(std::string("{}"))
                .timeout_connect(1)
                .timeout_max(2)
                .perform_sync();
        } catch (...) {}
        for (int attempt = 0; attempt < 20 && g_anycubic_bridge_process->running(); ++attempt)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (g_anycubic_bridge_process->running())
            g_anycubic_bridge_process->terminate();
        g_anycubic_bridge_process->wait();
    }
    g_anycubic_bridge_process.reset();
    g_anycubic_bridge_host.clear();
    g_anycubic_bridge_token.clear();
}

std::string AnycubicLink::make_url(const std::string& path) const
{
    std::string clean_path = path;
    if (!clean_path.empty() && clean_path[0] == '/')
        clean_path = clean_path.substr(1);
    return (boost::format("http://%1%:%2%/%3%") % m_host % m_port % clean_path).str();
}

std::string AnycubicLink::get_print_host_webui(DynamicPrintConfig *config)
{
    if (config == nullptr)
        return {};

    std::string webui = safe_config_str(config, "print_host_webui");
    if (!webui.empty())
        return webui;

    boost::filesystem::path local_html = boost::filesystem::path(Slic3r::resources_dir()) / "web" / "anycubic" / "workbench.html";
    if (boost::filesystem::exists(local_html)) {
        std::string p = local_html.generic_string();
        if (!p.empty() && p[0] != '/')
            p = "/" + p;
        return "file://" + p;
    }

    return "https://cloud-universe.anycubic.com/w/p/AcOrcaWeb/workbench/";
}

bool AnycubicLink::query_info(wxString& error_msg) const
{
    if (m_host.empty()) {
        error_msg = _(L("Printer host IP address is empty."));
        return false;
    }

    auto url = make_url("info");
    bool success = false;
    std::string response_body;

    auto http = Http::get(url);
    http.header("User-Agent", "AnycubicSlicerNext/2.0.0.2")
        .timeout_connect(5)
        .timeout_max(15)
        .on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = std::move(body);
                success = true;
            } else {
                error_msg = GUI::from_u8((boost::format("HTTP status %1%") % status).str());
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            error_msg = GUI::from_u8((boost::format("%1% (HTTP %2%)") % error % status).str());
        })
        .perform_sync();

    if (!success)
        return false;

    auto parsed = json::parse(response_body, nullptr, false, true);
    if (parsed.is_discarded() || !parsed.is_object()) {
        error_msg = _(L("Invalid JSON response from printer info endpoint."));
        return false;
    }

    auto* self = const_cast<AnycubicLink*>(this);
    self->m_token       = parsed.value("token", "");
    self->m_mode_id     = parsed.value("modelId", "");
    self->m_device_name = parsed.value("deviceName", parsed.value("modelName", "Anycubic Printer"));
    self->m_cn          = parsed.value("cn", "");
    self->m_ctrl_url    = parsed.value("ctrlInfoUrl", "");

    return true;
}

bool AnycubicLink::fetch_credentials(wxString& error_msg) const
{
    if (m_token.empty()) {
        if (!query_info(error_msg))
            return false;
    }

    if (m_token.size() < 16) {
        error_msg = _(L("Printer token is too short or invalid for signature."));
        return false;
    }

    uint64_t ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string nonce = generate_nonce(6);
    std::string token_prefix_md5 = md5_hex(m_token.substr(0, 16));
    std::string sign = md5_hex(token_prefix_md5 + std::to_string(ts) + nonce);
    std::string did = "orca-" + (m_cn.empty() ? generate_nonce(8) : m_cn);

    std::string ctrl_base = m_ctrl_url.empty() ? make_url("ctrl") : m_ctrl_url;
    std::string ctrl_url = (boost::format("%1%?ts=%2%&nonce=%3%&sign=%4%&did=%5%")
        % ctrl_base % ts % nonce % sign % did).str();

    bool success = false;
    std::string response_body;

    auto http = Http::post(ctrl_url);
    http.header("User-Agent", "AnycubicSlicerNext/2.0.0.2")
        .header("Content-Length", "0")
        .timeout_connect(5)
        .timeout_max(15)
        .on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                response_body = std::move(body);
                success = true;
            } else {
                error_msg = GUI::from_u8((boost::format("HTTP %1%: %2%") % status % body).str());
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            error_msg = GUI::from_u8((boost::format("%1% (HTTP %2%)") % error % status).str());
        })
        .perform_sync();

    if (!success)
        return false;

    auto parsed = json::parse(response_body, nullptr, false, true);
    if (parsed.is_discarded() || !parsed.contains("data") || !parsed["data"].is_object()) {
        error_msg = _(L("Invalid response from /ctrl endpoint."));
        return false;
    }

    std::string encrypted_b64 = parsed["data"].value("info", "");
    std::string iv_str = parsed["data"].value("token", "");
    if (encrypted_b64.empty() || iv_str.empty()) {
        error_msg = _(L("Missing encrypted credential payload or IV in /ctrl response."));
        return false;
    }

    std::vector<unsigned char> ciphertext;
    if (!base64_decode(encrypted_b64, ciphertext)) {
        error_msg = _(L("Failed to decode base64 credential payload from /ctrl."));
        return false;
    }

    std::string key_str = m_token.size() >= 32 ? m_token.substr(16, 16) : m_token.substr(16);
    std::string decrypted_json;
    if (!aes128_cbc_decrypt(ciphertext, key_str, iv_str, decrypted_json)) {
        error_msg = _(L("Failed to decrypt credentials from /ctrl response."));
        return false;
    }

    auto cred_json = json::parse(decrypted_json, nullptr, false, true);
    if (cred_json.is_discarded() || !cred_json.is_object()) {
        error_msg = _(L("Failed to parse decrypted credentials JSON."));
        return false;
    }

    auto* self = const_cast<AnycubicLink*>(this);
    self->m_device_id  = cred_json.value("deviceId", "");
    self->m_mode_id    = cred_json.value("modeId", cred_json.value("modelId", self->m_mode_id));
    self->m_broker     = cred_json.value("broker", "");
    self->m_username   = cred_json.value("username", "");
    self->m_password   = cred_json.value("password", "");
    self->m_device_crt = cred_json.value("devicecrt", "");
    self->m_device_pk  = cred_json.value("devicepk", "");
    if (cred_json.contains("modelName") && !cred_json["modelName"].get<std::string>().empty())
        self->m_device_name = cred_json["modelName"].get<std::string>();

    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Credential handshake succeeded for configured printer";
    return true;
}

bool AnycubicLink::fetch_upload_url_via_mqtt(std::string& upload_token, wxString& error_msg) const
{
    // First, check if our high-performance local LAN bridge daemon is running on localhost
    // which maintains a robust persistent MQTT TLS connection and always has the latest upload URL.
    try {
        auto http_local = Http::get("http://127.0.0.1:18988/upload-token");
        bool local_ok = false;
        std::string local_body;
        http_local.header("X-OrcaCubic-Token", anycubic_lan_bridge_token())
                  .timeout_connect(1)
                  .timeout_max(2)
                  .on_complete([&](std::string body, unsigned status) {
                      if (status == 200) {
                          local_ok = true;
                          local_body = std::move(body);
                      }
                  })
                  .perform_sync();
        if (local_ok) {
            auto parsed = json::parse(local_body, nullptr, false, true);
            if (!parsed.is_discarded() && parsed.value("ip", "") == m_host) {
                std::string token_value = parsed.value("upload_token", "");
                if (!token_value.empty()) {
                    upload_token = std::move(token_value);
                    const_cast<AnycubicLink*>(this)->m_upload_token = upload_token;
                    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Acquired session upload token via local LAN bridge: [REDACTED]";
                    return true;
                }
            }
        }
    } catch (...) {}

    if (m_device_id.empty() || m_username.empty() || m_device_crt.empty() || m_device_pk.empty()) {
        if (!fetch_credentials(error_msg))
            return false;
    }

    std::string mqtt_port = "9883";
    if (!m_broker.empty()) {
        auto colon = m_broker.rfind(':');
        if (colon != std::string::npos)
            mqtt_port = m_broker.substr(colon + 1);
    }

    std::string client_id = "orca-" + (m_device_id.size() >= 8 ? m_device_id.substr(0, 8) : generate_nonce(8));
    auto mqtt = std::make_shared<MqttTlsSession>();
    if (!mqtt->connect(m_host, mqtt_port, m_username, m_password, m_device_crt, m_device_pk, client_id, error_msg)) {
        BOOST_LOG_TRIVIAL(warning) << "[AnycubicLink] MQTT connect failed during upload token fetch: " << error_msg.ToUTF8().data();
        return false;
    }

    std::string sub_topic = (boost::format("anycubic/anycubicCloud/v1/printer/public/%1%/%2%/#") % m_mode_id % m_device_id).str();
    if (!mqtt->subscribe(1, sub_topic, error_msg))
        return false;

    std::string pub_topic = (boost::format("anycubic/anycubicCloud/v1/slicer/printer/%1%/%2%/info") % m_mode_id % m_device_id).str();
    json query_req = {
        {"type", "info"},
        {"action", "query"},
        {"msgid", generate_uuid()},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
        {"data", json::object()}
    };

    if (!mqtt->publish(pub_topic, query_req.dump(), error_msg))
        return false;

    std::string resp_json;
    if (mqtt->read_json_response(resp_json, 4)) {
        auto parsed = json::parse(resp_json, nullptr, false, true);
        if (!parsed.is_discarded() && parsed.contains("data") && parsed["data"].is_object() && parsed["data"].contains("urls")) {
            std::string file_upload_url = parsed["data"]["urls"].value("fileUploadurl", "");
            auto s_idx = file_upload_url.find("?s=");
            if (s_idx != std::string::npos) {
                upload_token = file_upload_url.substr(s_idx + 3);
                const_cast<AnycubicLink*>(this)->m_upload_token = upload_token;
                BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Acquired session upload token via MQTT info report: [REDACTED]";
                return true;
            }
        }
    }

    error_msg = _(L("No upload token received from MQTT info report."));
    return false;
}

bool AnycubicLink::start_print(wxString& error_msg, const std::string& filename, const PrintHostUpload& upload_data) const
{
    bool use_ams = true;
    json ams_box_mapping = json::array();

    std::string custom_mapping = upload_data.extended("ams_mapping");
    if (!custom_mapping.empty()) {
        try {
            ams_box_mapping = json::parse(custom_mapping);
            use_ams = true;
        } catch (...) {}
    }

    if (ams_box_mapping.empty()) {
        auto parse_rgb = [](const std::string& hex, int& r, int& g, int& b) {
            std::string h = hex;
            if (!h.empty() && h[0] == '#') h = h.substr(1);
            if (h.size() >= 6) {
                try {
                    r = std::stoi(h.substr(0, 2), nullptr, 16);
                    g = std::stoi(h.substr(2, 2), nullptr, 16);
                    b = std::stoi(h.substr(4, 2), nullptr, 16);
                } catch (...) { r = 35; g = 163; b = 199; }
            } else {
                r = 35; g = 163; b = 199;
            }
        };

        // If plate_extruders was explicitly resolved by the slicer (e.g. "1,4")
        std::string extruders_str = upload_data.extended("plate_extruders");
        std::vector<int> used_extruders;
        if (!extruders_str.empty()) {
            std::vector<std::string> tokens;
            boost::split(tokens, extruders_str, boost::is_any_of(","));
            for (const auto& tok : tokens) {
                try {
                    int val = std::stoi(tok);
                    if (val >= 1 && val <= 4)
                        used_extruders.push_back(val);
                } catch (...) {}
            }
        }

        if (GUI::wxGetApp().preset_bundle) {
            auto full_cfg = GUI::wxGetApp().preset_bundle->full_config();
            const auto* colors = full_cfg.option<ConfigOptionStrings>("filament_colour");
            const auto* types  = full_cfg.option<ConfigOptionStrings>("filament_type");
            
            if (!used_extruders.empty()) {
                // Map only the specific extruders used by this print plate
                for (int ext_1based : used_extruders) {
                    size_t idx = static_cast<size_t>(ext_1based - 1); // 0-based
                    std::string c_hex = (colors && idx < colors->values.size()) ? colors->values[idx] : "#23a3c7";
                    std::string m_type = (types && idx < types->values.size()) ? types->values[idx] : "PLA";
                    int r = 35, g = 163, b = 199;
                    parse_rgb(c_hex, r, g, b);
                    ams_box_mapping.push_back({
                        {"ams_index", static_cast<int>(idx)},    // maps to ACE Pro slot index (0 = slot 1, 3 = slot 4)
                        {"paint_index", static_cast<int>(idx)},  // matches G-code extruder index T0, T3
                        {"material_type", m_type},
                        {"ams_color", {r, g, b}},
                        {"paint_color", {r, g, b}}
                    });
                }
            } else {
                // Default full 4-slot 1-to-1 mapping
                size_t count = colors ? colors->values.size() : (types ? types->values.size() : 4);
                count = std::min(count, size_t(4));
                for (size_t i = 0; i < count; ++i) {
                    std::string c_hex = (colors && i < colors->values.size()) ? colors->values[i] : "#23a3c7";
                    std::string m_type = (types && i < types->values.size()) ? types->values[i] : "PLA";
                    int r = 35, g = 163, b = 199;
                    parse_rgb(c_hex, r, g, b);
                    ams_box_mapping.push_back({
                        {"ams_index", static_cast<int>(i)},
                        {"paint_index", static_cast<int>(i)},
                        {"material_type", m_type},
                        {"ams_color", {r, g, b}},
                        {"paint_color", {r, g, b}}
                    });
                }
            }
        }
        if (ams_box_mapping.empty()) {
            for (int i = 0; i < 4; ++i) {
                ams_box_mapping.push_back({
                    {"ams_index", i},
                    {"paint_index", i},
                    {"material_type", "PLA"},
                    {"ams_color", {35, 163, 199}},
                    {"paint_color", {35, 163, 199}}
                });
            }
        }
    }

    const auto read_toggle = [&upload_data](const char* key, bool fallback) {
        const std::string value = upload_data.extended(key);
        return value.empty() ? fallback : value == "1";
    };
    const AnycubicTaskSettings task_settings = build_anycubic_task_settings({
        read_toggle("auto_leveling", true),
        read_toggle("vibration_compensation", false),
        read_toggle("flow_calibration", false),
        read_toggle("timelapse", false)
    });

    // First, attempt to trigger print via our local daemon if available
    try {
        auto http_local = Http::post("http://127.0.0.1:18988/control");
        json ctrl_data = {
            {"action", "start_print_job"},
            {"filename", filename},
            {"ams_box_mapping", ams_box_mapping},
            {"task_settings", {
                {"auto_leveling", task_settings.auto_leveling},
                {"vibration_compensation", task_settings.vibration_compensation},
                {"flow_calibration", task_settings.flow_calibration},
                {"timelapse", task_settings.timelapse_status}
            }}
        };
        bool local_ok = false;
        std::string local_body;
        http_local.header("Content-Type", "application/json")
                  .header("X-OrcaCubic-Token", anycubic_lan_bridge_token())
                  .set_post_body(ctrl_data.dump())
                  .timeout_connect(1)
                  .timeout_max(3)
                  .on_complete([&](std::string body, unsigned status) {
                      if (status == 200) {
                          local_ok = true;
                          local_body = std::move(body);
                      }
                  })
                  .perform_sync();
        if (local_ok) {
            auto parsed = json::parse(local_body, nullptr, false, true);
            if (!parsed.is_discarded() && parsed.value("status", "") == "ok" && parsed.value("ip", "") == m_host) {
                BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Successfully triggered print via local LAN bridge: " << filename;
                return true;
            }
        }
    } catch (...) {}

    if (m_device_id.empty() || m_username.empty() || m_device_crt.empty() || m_device_pk.empty()) {
        if (!fetch_credentials(error_msg))
            return false;
    }

    std::string mqtt_port = "9883";
    if (!m_broker.empty()) {
        auto colon = m_broker.rfind(':');
        if (colon != std::string::npos)
            mqtt_port = m_broker.substr(colon + 1);
    }

    std::string client_id = "orca-" + (m_device_id.size() >= 8 ? m_device_id.substr(0, 8) : generate_nonce(8));
    auto mqtt = std::make_shared<MqttTlsSession>();
    if (!mqtt->connect(m_host, mqtt_port, m_username, m_password, m_device_crt, m_device_pk, client_id, error_msg)) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] MQTT connect failed for start_print: " << error_msg.ToUTF8().data();
        return false;
    }

    std::string sub_topic = (boost::format("anycubic/anycubicCloud/v1/printer/public/%1%/%2%/#") % m_mode_id % m_device_id).str();
    wxString sub_err;
    mqtt->subscribe(1, sub_topic, sub_err);

    std::string pub_topic = (boost::format("anycubic/anycubicCloud/v1/slicer/printer/%1%/%2%/print") % m_mode_id % m_device_id).str();

    size_t file_size = 0;
    try {
        file_size = fs::file_size(upload_data.source_path);
    } catch (...) {}

    std::string file_md5;
    try {
        std::string src_path = upload_data.source_path.string();
        bbl_calc_md5(src_path, file_md5);
        boost::algorithm::to_lower(file_md5);
    } catch (...) {}

    json payload = {
        {"type", "print"},
        {"action", "start"},
        {"msgid", generate_uuid()},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
        {"data", {
            {"taskid", "-1"},
            {"filename", filename},
            {"url", ""},
            {"md5", file_md5},
            {"filepath", nullptr},
            {"filetype", 1},
            {"project_type", 1},
            {"filesize", file_size},
            {"ams_settings", {
                {"use_ams", use_ams},
                {"ams_box_mapping", ams_box_mapping}
            }},
            {"task_settings", {
                {"auto_leveling", task_settings.auto_leveling},
                {"vibration_compensation", task_settings.vibration_compensation},
                {"flow_calibration", task_settings.flow_calibration},
                {"dry_mode", 0},
                {"ai_settings", {{"status", 0}, {"count", 0}, {"type", 0}}},
                {"timelapse", {{"status", task_settings.timelapse_status}, {"count", 0}, {"type", 0}}},
                {"drying_settings", {{"status", 0}, {"target_temp", 0}, {"duration", 0}, {"remain_time", 0}}},
                {"model_objects_skip_parts", json::array()}
            }}
        }}
    };

    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Sending print start command for " << filename << " to " << pub_topic;
    if (!mqtt->publish(pub_topic, payload.dump(), error_msg)) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] Failed to publish start_print command: " << error_msg.ToUTF8().data();
        return false;
    }

    std::string print_ack_json;
    if (mqtt->read_json_response(print_ack_json, 3)) {
        BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Received response after print start: " << print_ack_json;
    }

    return true;
}

bool AnycubicLink::test(wxString &curl_msg) const
{
    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] test() start for host " << m_host << ":" << m_port;
    wxString error;
    if (!query_info(error)) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] query_info failed: " << error.ToUTF8().data();
        curl_msg = error;
        return false;
    }

    if (!fetch_credentials(error)) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] fetch_credentials failed: " << error.ToUTF8().data();
        curl_msg = error;
        return false;
    }

    curl_msg = GUI::from_u8((boost::format("%1% (Model ID: %2%, Device ID: %3%, CN: %4%)") % m_device_name % m_mode_id % m_device_id % m_cn).str());
    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] test() success: " << curl_msg.ToUTF8().data();
    return true;
}

wxString AnycubicLink::get_test_ok_msg() const
{
    return GUI::from_u8((boost::format(_utf8(L("Connected to %1% successfully over LAN."))) % m_device_name).str());
}

wxString AnycubicLink::get_test_failed_msg(wxString &msg) const
{
    return GUI::from_u8((boost::format(_utf8(L("Could not connect to Anycubic printer at %1%:%2%: %3%"))) % m_host % m_port % msg.ToUTF8().data()).str());
}

bool AnycubicLink::upload(PrintHostUpload upload_data, ProgressFn progress_fn, ErrorFn error_fn, InfoFn info_fn) const
{
    wxString query_err;
    if (!query_info(query_err)) {
        error_fn(GUI::from_u8((boost::format("Failed to reach printer before upload: %1%") % query_err.ToUTF8().data()).str()));
        return false;
    }

    // Ensure we have device credentials and MQTT parameters
    wxString cred_err;
    if (m_device_id.empty() && !fetch_credentials(cred_err)) {
        BOOST_LOG_TRIVIAL(warning) << "[AnycubicLink] Credential fetch warning: " << cred_err.ToUTF8().data();
    }

    // Acquire session upload token via MQTT info query
    std::string upload_token;
    wxString mqtt_err;
    if (!fetch_upload_url_via_mqtt(upload_token, mqtt_err)) {
        BOOST_LOG_TRIVIAL(warning) << "[AnycubicLink] MQTT upload token fetch warning: " << mqtt_err.ToUTF8().data();
        upload_token = m_token;
    }

    std::string upload_filename = sanitize_anycubic_filename(upload_data.upload_path.string());
    std::string file_size_str;
    try {
        file_size_str = std::to_string(fs::file_size(upload_data.source_path));
    } catch (...) {
        file_size_str = "0";
    }

    std::string upload_endpoint = "gcode_upload";
    if (!upload_token.empty())
        upload_endpoint += "?s=" + upload_token;

    auto url = make_url(upload_endpoint);
    bool upload_ok = false;
    std::string response_payload;

    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Uploading file " << upload_filename << " (" << file_size_str << " bytes) to configured printer";

    auto http = Http::post(url);
    http.header("User-Agent", "AnycubicSlicerNext/2.0.0.2")
        .header("X-BBL-Client-Name", "AnycubicSlicerNext")
        .header("X-BBL-Client-Type", "slicer")
        .header("X-BBL-Client-Version", "01.03.09.04")
        .header("X-File-Length", file_size_str)
        .form_add("filename", upload_filename)
        .form_add_file("gcode", upload_data.source_path.string(), upload_filename)
        .on_complete([&](std::string body, unsigned status) {
            BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Upload complete with status " << status << ", body: " << body;
            if (status == 200) {
                upload_ok = true;
                response_payload = std::move(body);
            } else {
                error_fn(GUI::from_u8((boost::format("Printer upload returned HTTP %1%: %2%") % status % body).str()));
            }
        })
        .on_error([&](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] Upload error: " << error << " HTTP " << status;
            error_fn(GUI::from_u8((boost::format("Upload failed: %1% (HTTP %2%)") % error % status).str()));
        })
        .on_progress([&](Http::Progress progress, bool& cancel) {
            progress_fn(std::move(progress), cancel);
            if (cancel)
                upload_ok = false;
        })
        .perform_sync();

    if (!upload_ok)
        return false;

    if (upload_data.post_action == PrintHostPostUploadAction::StartPrint) {
        info_fn("AnycubicLink", GUI::from_u8((boost::format(_utf8(L("File uploaded. Triggering print on %1%..."))) % m_device_name).str()));
        wxString start_err;
        if (!start_print(start_err, upload_filename, upload_data)) {
            error_fn(GUI::from_u8((boost::format(_utf8(L("File uploaded, but failed to start print: %1%"))) % start_err.ToUTF8().data()).str()));
            return false;
        }
        info_fn("AnycubicLink", GUI::from_u8((boost::format(_utf8(L("Print started successfully on %1%."))) % m_device_name).str()));
    }

    return true;
}

bool AnycubicLink::fetch_material_slots(std::vector<AnycubicMaterialSlot>& slots, wxString& msg) const
{
    slots.clear();
    bool success = false;
    std::string response_body;
    auto http = Http::get("http://127.0.0.1:18988/status");
    http.header("X-OrcaCubic-Token", anycubic_lan_bridge_token())
        .timeout_connect(1)
        .timeout_max(3)
        .on_complete([&](std::string body, unsigned status) {
            if (status == 200) {
                success = true;
                response_body = std::move(body);
            }
        })
        .on_error([&](std::string, std::string error, unsigned) {
            msg = GUI::from_u8(error);
        })
        .perform_sync();

    if (!success) {
        if (msg.empty())
            msg = _(L("Unable to read ACE Pro material slots from the local LAN bridge."));
        return false;
    }

    const auto payload = json::parse(response_body, nullptr, false, true);
    if (payload.is_discarded() || payload.value("ip", "") != m_host || !payload.value("connected", false) || !payload.contains("filaments") || !payload["filaments"].is_array()) {
        msg = _(L("The local LAN bridge did not report connected ACE Pro slots."));
        return false;
    }

    for (const auto& item : payload["filaments"]) {
        AnycubicMaterialSlot slot;
        slot.slot_id = item.value("slot", -1);
        slot.box_id  = -1;
        slot.type    = item.value("type", "");
        slot.color   = item.value("color", "#D0D0D0");
        // The daemon reports the active feeder with `loaded`; every reported slot
        // still contains a spool and is valid for mapping.
        slot.loaded  = slot.slot_id >= 1 && slot.slot_id <= 4 && !slot.type.empty();
        if (slot.loaded)
            slots.push_back(std::move(slot));
    }

    if (slots.empty()) {
        msg = _(L("No usable ACE Pro material slots were reported."));
        return false;
    }
    return true;
}

} // namespace Slic3r
