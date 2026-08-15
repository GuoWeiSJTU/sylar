#ifndef __SYLAR_TCP_SERVER_H__
#define __SYLAR_TCP_SERVER_H__

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "address.h"
#include "config.h"
#include "coroutine/task.h"
#include "net/async_tcp_server.h"

namespace sylar {

struct TcpServerConf {
    using ptr = std::shared_ptr<TcpServerConf>;
    std::vector<std::string> address;
    int keepalive = 0;
    int timeout = 1000 * 2 * 60;
    int ssl = 0;
    std::string id;
    std::string type = "http";
    std::string name;
    std::string cert_file;
    std::string key_file;
    // Kept for configuration compatibility; coroutine runtime owns all work.
    std::string async_runtime = "coroutine";
    std::map<std::string, std::string> args;

    bool isValid() const { return !address.empty(); }
    bool operator==(const TcpServerConf& other) const {
        return address == other.address && keepalive == other.keepalive &&
               timeout == other.timeout && ssl == other.ssl && id == other.id &&
               type == other.type && name == other.name &&
               cert_file == other.cert_file && key_file == other.key_file &&
               async_runtime == other.async_runtime && args == other.args;
    }
};

template<> class LexicalCast<std::string, TcpServerConf> {
public:
    TcpServerConf operator()(const std::string& value) {
        YAML::Node node = YAML::Load(value);
        TcpServerConf result;
        result.id = node["id"].as<std::string>(result.id);
        result.type = node["type"].as<std::string>(result.type);
        result.keepalive = node["keepalive"].as<int>(result.keepalive);
        result.timeout = node["timeout"].as<int>(result.timeout);
        result.name = node["name"].as<std::string>(result.name);
        result.ssl = node["ssl"].as<int>(result.ssl);
        result.cert_file = node["cert_file"].as<std::string>(result.cert_file);
        result.key_file = node["key_file"].as<std::string>(result.key_file);
        result.async_runtime = node["async_runtime"].as<std::string>(result.async_runtime);
        result.args = LexicalCast<std::string, std::map<std::string, std::string>>()(
            node["args"].as<std::string>(""));
        if(node["address"].IsDefined()) {
            for(size_t i = 0; i < node["address"].size(); ++i) {
                result.address.push_back(node["address"][i].as<std::string>());
            }
        }
        return result;
    }
};

template<> class LexicalCast<TcpServerConf, std::string> {
public:
    std::string operator()(const TcpServerConf& conf) {
        YAML::Node node;
        node["id"] = conf.id;
        node["type"] = conf.type;
        node["name"] = conf.name;
        node["keepalive"] = conf.keepalive;
        node["timeout"] = conf.timeout;
        node["ssl"] = conf.ssl;
        node["cert_file"] = conf.cert_file;
        node["key_file"] = conf.key_file;
        node["async_runtime"] = conf.async_runtime;
        node["args"] = YAML::Load(LexicalCast<std::map<std::string, std::string>, std::string>()(conf.args));
        for(const auto& address : conf.address) node["address"].push_back(address);
        std::stringstream stream;
        stream << node;
        return stream.str();
    }
};

class TcpServer : public AsyncTcpServer {
public:
    using ptr = std::shared_ptr<TcpServer>;
    explicit TcpServer(size_t thread_count = 0);
    ~TcpServer() override = default;

    bool bind(Address::ptr address, bool ssl = false) override;
    bool bind(const std::vector<Address::ptr>& addresses,
              std::vector<Address::ptr>& failures, bool ssl = false) override;
    bool start() override;
    void stop() override;

    bool loadCertificates(const std::string&, const std::string&);
    uint64_t getRecvTimeout() const { return m_recv_timeout; }
    void setRecvTimeout(uint64_t value) { m_recv_timeout = value; }
    const std::string& getName() const { return m_name; }
    virtual void setName(const std::string& value) { m_name = value; }
    bool isStop() const { return isStopped(); }
    TcpServerConf::ptr getConf() const { return m_conf; }
    void setConf(TcpServerConf::ptr value) { m_conf = std::move(value); }
    void setConf(const TcpServerConf& value);
    const std::string& getType() const { return m_type; }
    virtual std::string toString(const std::string& prefix = "") const;

protected:
    uint64_t m_recv_timeout;
    std::string m_name;
    std::string m_type = "tcp";
    bool m_ssl = false;
    TcpServerConf::ptr m_conf;
};

}

#endif
