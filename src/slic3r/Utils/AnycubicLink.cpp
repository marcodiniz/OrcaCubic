#include "AnycubicLink.hpp"

#include <algorithm>
#include <chrono>
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
#include <nlohmann/json.hpp>

#include <openssl/md5.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "Http.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Utils.hpp"

namespace fs = boost::filesystem;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

namespace Slic3r {

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
            boost::system::error_code ec;
            m_socket->close(ec);
            m_socket.reset();
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

    // Default to official Anycubic Workbench web UI
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
        .timeout_connect(5)
        .timeout_max(15)
        .set_post_body(std::string("{}"))
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

    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Credential handshake succeeded. Device ID: " << self->m_device_id
                            << ", Mode ID: " << self->m_mode_id
                            << ", Broker: " << self->m_broker;
    return true;
}

bool AnycubicLink::fetch_upload_url_via_mqtt(std::string& upload_token, wxString& error_msg) const
{
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
    MqttTlsSession mqtt;
    if (!mqtt.connect(m_host, mqtt_port, m_username, m_password, m_device_crt, m_device_pk, client_id, error_msg)) {
        BOOST_LOG_TRIVIAL(warning) << "[AnycubicLink] MQTT connect failed during upload token fetch: " << error_msg.ToUTF8().data();
        return false;
    }

    std::string sub_topic = (boost::format("anycubic/anycubicCloud/v1/printer/public/%1%/%2%/#") % m_mode_id % m_device_id).str();
    if (!mqtt.subscribe(1, sub_topic, error_msg))
        return false;

    std::string pub_topic = (boost::format("anycubic/anycubicCloud/v1/slicer/printer/%1%/%2%/info") % m_mode_id % m_device_id).str();
    json query_req = {
        {"type", "info"},
        {"action", "query"},
        {"msgid", generate_uuid()},
        {"timestamp", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
        {"data", json::object()}
    };

    if (!mqtt.publish(pub_topic, query_req.dump(), error_msg))
        return false;

    std::string resp_json;
    if (mqtt.read_json_response(resp_json, 4)) {
        auto parsed = json::parse(resp_json, nullptr, false, true);
        if (!parsed.is_discarded() && parsed.contains("data") && parsed["data"].is_object() && parsed["data"].contains("urls")) {
            std::string file_upload_url = parsed["data"]["urls"].value("fileUploadurl", "");
            auto s_idx = file_upload_url.find("?s=");
            if (s_idx != std::string::npos) {
                upload_token = file_upload_url.substr(s_idx + 3);
                const_cast<AnycubicLink*>(this)->m_upload_token = upload_token;
                BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Acquired session upload token via MQTT info report: " << upload_token;
                return true;
            }
        }
    }

    error_msg = _(L("No upload token received from MQTT info report."));
    return false;
}

bool AnycubicLink::start_print(wxString& error_msg, const std::string& filename, const PrintHostUpload& upload_data) const
{
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
    MqttTlsSession mqtt;
    if (!mqtt.connect(m_host, mqtt_port, m_username, m_password, m_device_crt, m_device_pk, client_id, error_msg)) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] MQTT connect failed for start_print: " << error_msg.ToUTF8().data();
        return false;
    }

    std::string sub_topic = (boost::format("anycubic/anycubicCloud/v1/printer/public/%1%/%2%/#") % m_mode_id % m_device_id).str();
    wxString sub_err;
    mqtt.subscribe(1, sub_topic, sub_err);

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
                {"use_ams", false},
                {"ams_box_mapping", json::array()}
            }},
            {"task_settings", {
                {"auto_leveling", 1},
                {"vibration_compensation", 0},
                {"flow_calibration", 0},
                {"dry_mode", 0},
                {"ai_settings", {{"status", 0}, {"count", 0}, {"type", 0}}},
                {"timelapse", {{"status", 0}, {"count", 0}, {"type", 0}}},
                {"drying_settings", {{"status", 0}, {"target_temp", 0}, {"duration", 0}, {"remain_time", 0}}},
                {"model_objects_skip_parts", json::array()}
            }}
        }}
    };

    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Sending print start command for " << filename << " to " << pub_topic;
    if (!mqtt.publish(pub_topic, payload.dump(), error_msg)) {
        BOOST_LOG_TRIVIAL(error) << "[AnycubicLink] Failed to publish start_print command: " << error_msg.ToUTF8().data();
        return false;
    }

    std::string print_ack_json;
    if (mqtt.read_json_response(print_ack_json, 3)) {
        BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Received response after print start: " << print_ack_json;
    }

    return true;
}

bool AnycubicLink::test(wxString &curl_msg) const
{
    wxString error;
    if (!query_info(error)) {
        curl_msg = error;
        return false;
    }

    if (!fetch_credentials(error)) {
        curl_msg = error;
        return false;
    }

    curl_msg = GUI::from_u8((boost::format("%1% (Model ID: %2%, Device ID: %3%, CN: %4%)") % m_device_name % m_mode_id % m_device_id % m_cn).str());
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

    BOOST_LOG_TRIVIAL(info) << "[AnycubicLink] Uploading file " << upload_filename << " (" << file_size_str << " bytes) to " << url;

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
    return true;
}

} // namespace Slic3r
