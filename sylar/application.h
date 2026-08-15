#ifndef __SYLAR_APPLICATION_H__
#define __SYLAR_APPLICATION_H__

#include <atomic>
#include <map>
#include <vector>

#include "sylar/http/http_server.h"
#include "sylar/http/ws_server.h"
#include "sylar/rock/rock_server.h"

namespace sylar {

class Application {
public:
    Application();
    static Application* GetInstance() { return s_instance; }
    bool init(int argc, char** argv);
    bool run();
    bool getServer(const std::string& type, std::vector<TcpServer::ptr>& servers);
    void listAllServer(std::map<std::string, std::vector<TcpServer::ptr>>& servers);
    void stop() { m_running.store(false); }

private:
    int main(int argc, char** argv);
    int run_servers();
    int m_argc = 0;
    char** m_argv = nullptr;
    std::map<std::string, std::vector<TcpServer::ptr>> m_servers;
    std::atomic<bool> m_running{true};
    static Application* s_instance;
};

}

#endif
