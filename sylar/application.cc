#include "application.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <fstream>
#include <thread>
#include <unistd.h>

#include "sylar/config.h"
#include "sylar/daemon.h"
#include "sylar/env.h"
#include "sylar/log.h"

namespace sylar {

namespace {
Logger::ptr g_logger = SYLAR_LOG_NAME("application");
ConfigVar<std::string>::ptr g_server_work_path = Config::Lookup(
    "server.work_path", std::string("/tmp/sylar"), "server work path");
ConfigVar<std::string>::ptr g_server_pid_file = Config::Lookup(
    "server.pid_file", std::string("sylar.pid"), "server pid file");
ConfigVar<std::vector<TcpServerConf>>::ptr g_servers_conf = Config::Lookup(
    "servers", std::vector<TcpServerConf>(), "coroutine server configuration");
Application* g_signal_application = nullptr;
void onSignal(int) { if(g_signal_application) g_signal_application->stop(); }
}

Application* Application::s_instance = nullptr;

Application::Application() { s_instance = this; }

bool Application::init(int argc, char** argv) {
    m_argc = argc;
    m_argv = argv;
    EnvMgr::GetInstance()->addHelp("s", "start with the terminal");
    EnvMgr::GetInstance()->addHelp("d", "run as daemon");
    EnvMgr::GetInstance()->addHelp("c", "conf path default: ./conf");
    EnvMgr::GetInstance()->addHelp("p", "print help");
    if(!EnvMgr::GetInstance()->init(argc, argv) || EnvMgr::GetInstance()->has("p")) {
        EnvMgr::GetInstance()->printHelp();
        return false;
    }
    Config::LoadFromConfDir(EnvMgr::GetInstance()->getConfigPath());
    if(!EnvMgr::GetInstance()->has("s") && !EnvMgr::GetInstance()->has("d")) {
        EnvMgr::GetInstance()->printHelp();
        return false;
    }
    const std::string path = g_server_work_path->getValue();
    if(!FSUtil::Mkdir(path)) {
        SYLAR_LOG_ERROR(g_logger) << "cannot create work path " << path;
        return false;
    }
    return true;
}

bool Application::run() {
    const bool daemon_mode = EnvMgr::GetInstance()->has("d");
    return start_daemon(m_argc, m_argv,
        [this](int argc, char** argv) { return main(argc, argv); }, daemon_mode) == 0;
}

int Application::main(int, char**) {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    g_signal_application = this;
    const std::string pidfile = g_server_work_path->getValue() + "/" + g_server_pid_file->getValue();
    std::ofstream(pidfile) << getpid();
    const int result = run_servers();
    while(m_running.load()) std::this_thread::sleep_for(std::chrono::seconds(1));
    for(auto& group : m_servers) for(auto& server : group.second) server->stop();
    unlink(pidfile.c_str());
    return result;
}

int Application::run_servers() {
    for(const auto& conf : g_servers_conf->getValue()) {
        if(conf.async_runtime != "coroutine") {
            SYLAR_LOG_ERROR(g_logger) << "only async_runtime=coroutine is supported";
            return -1;
        }
        std::vector<Address::ptr> addresses;
        for(const auto& value : conf.address) {
            const size_t colon = value.rfind(':');
            if(colon == std::string::npos) addresses.push_back(Address::LookupAny(value));
            else addresses.push_back(IPAddress::Create(value.substr(0, colon).c_str(),
                                                        static_cast<uint16_t>(std::stoi(value.substr(colon + 1)))));
        }
        addresses.erase(std::remove(addresses.begin(), addresses.end(), nullptr), addresses.end());
        TcpServer::ptr server;
        if(conf.type == "http") server = std::make_shared<http::HttpServer>(conf.keepalive != 0);
        else if(conf.type == "ws") server = std::make_shared<http::WSServer>();
        else if(conf.type == "rock" || conf.type == "nameserver") server = std::make_shared<RockServer>(conf.type);
        else { SYLAR_LOG_ERROR(g_logger) << "unknown server type " << conf.type; return -1; }
        if(!conf.name.empty()) server->setName(conf.name);
        std::vector<Address::ptr> failures;
        if(!server->bind(addresses, failures, conf.ssl != 0) || !server->start()) return -1;
        server->setConf(conf);
        m_servers[conf.type].push_back(std::move(server));
    }
    return 0;
}

bool Application::getServer(const std::string& type, std::vector<TcpServer::ptr>& servers) {
    auto it = m_servers.find(type);
    if(it == m_servers.end()) return false;
    servers = it->second;
    return true;
}

void Application::listAllServer(std::map<std::string, std::vector<TcpServer::ptr>>& servers) {
    servers = m_servers;
}

}
