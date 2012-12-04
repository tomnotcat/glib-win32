/* Oren - a streaming media software development kit.
 *
 * Copyright ?2012 TinySoft, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __OREN_CLIENT_CXX_H__
#define __OREN_CLIENT_CXX_H__

#ifdef WIN32
#ifdef OREN_EXPORTS
#define OREN_PUBLIC __declspec(dllexport)
#else
#define OREN_PUBLIC __declspec(dllimport)
#endif
#else  /* !WIN32 */
#define OREN_PUBLIC
#endif  /* !WIN32 */

#include <list>
#include <string>

class OREN_PUBLIC COrenClient
{
public:
    enum Consts {
        PingTimeout = -1,
        PacketSize = 1200
    };

    enum OnlineState {
        StateUnknown,
        StateLogining,
        StateOnline,
        StateOffline
    };

    enum LoginResult {
        LoginUnknown,        // 未知错误
        LoginSuccess,        // 登录成功
        LoginTimeout,        // 登录超时
        LoginErrCliVer,      // 客户端版本错误
        LoginErrSvrVer,      // 服务器版本错误
        LoginErrSignature,   // 登录签名错误
        LoginErrUserName,    // 非法用户名
        LoginErrCnlName,     // 非法频道名
        LoginErrOverload,    // 服务器超载
        LoginErrSize,        // 数据包大小错误
        LoginErrLoginCode,   // 登录码错误
        LoginErrAddress,     // 获取频道地址失败
        LoginNoSvrName,      // 获取服务器名字失败
        LoginNoCliName,      // 获取客户端名字失败
        LoginNoSvrVer,       // 获取服务器版本失败
        LoginNoCliVer,       // 获取客户端版本失败
        LoginNoNetType,      // 获取网络类型失败
        LoginNoSiganture,    // 获取签名失败
        LoginServerInit,     // 服务器初始化中
        LoginErrCMRequest    // 获取服务器列表失败
    };

    enum LogoutReason {
        LogoutUnknown,       // 未知错误
        LogoutNormal,        // 正常登出
        LogoutDisconnect,    // 与服务器失去连接
        LogoutKickout,       // 被踢出频道
        LogoutFrozen         // 频道总结
    };

    struct ServerInfo {
    public:
        ServerInfo (const char *n, const char *g, const char *a)
            : name (n)
            , group (g)
            , address (a)
        {
        }

    public:
        std::string name;
        std::string group;
        // 127.0.0.1:9001
        std::string address;
    };

    struct Statistics {
        unsigned int ping;
        unsigned int generate;
        unsigned int send_data;
        unsigned int recv_data;
        unsigned int send_retry;
        unsigned int recv_retry;
        unsigned int send_lost;
        unsigned int recv_lost;
        unsigned int duplicate;
    };

    typedef std::list<ServerInfo> ServerList;

public:
    // 127.0.0.1:9001
    void Ping (const char *smaddr, const char *channel, int timeout);

    // cm://127.0.0.1:7474 OR dc://127.0.0.1:9001
    void Login (const char *address, const char *channel, const char *user);

    // 127.0.0.1:7474
    void ReqServers (const char *cmaddr, const char *channel);

    void SetQuality (unsigned int quality);

    unsigned int GetQuality (void);

    void SetSource (const void *param, size_t size);

    void SendFormat (const void *format, size_t size);

    void TurnRecv (bool accept_data);

    void SendData (const void *data, size_t size);

    void Logout (void);

    OnlineState GetState (void);

    Statistics GetStatistics (void);

    const char* ServerName (void);

    const char* ServerVersion (void);

public:
    // 127.0.0.1:9001
    virtual void OnPing (const char *address, int rtt,
                         const char *channel, int user_count);

    virtual void OnChoose (const char *server_name);

    virtual void OnTimeOffset (int msec);

    virtual void OnLogin (LoginResult result);

    virtual void OnLogout (LogoutReason reason);

    virtual void OnServerList (const ServerList &list);

    virtual void OnSource (const char *source_name,
                           const void *param,
                           size_t size);

    virtual void OnQuality (unsigned int quality);

    virtual void OnFormat (const void *format, size_t size);

    virtual void OnTurnSend (bool turn);

    virtual void OnTurnRecv (bool turn);

    virtual void OnData (const void *data, size_t size);

public:
    COrenClient (const char *signature = "");
    ~COrenClient (void);

    static void Initialize (const char *app_path,
                            const char *log_path,
                            int log_level,
                            int log_max_rps);

    static void Uninitialize (void);

    static const char* LoginResultToString (LoginResult result);

    static const char* LogoutReasonToString (LogoutReason reason);

private:
    COrenClient (const COrenClient&);
    COrenClient& operator= (const COrenClient&);

private:
    class COrenClientImpl;
    COrenClientImpl *m_impl;
};

#endif /* __OREN_CLIENT_CXX_H__ */
