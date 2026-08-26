# ============================================================
# net.im - Infiverse 项目网络设定
# 使用方式: import "net.im" 后调用 net_* 函数
# 频道模型: 每个层级(layer)一个频道, 消息按层级隔离
# ============================================================

# ---- 服务器设定 ----
SERVER_HOST = "hub.example.com"
SERVER_PORT = 8080
SERVER_ID = "verse://" + SERVER_HOST + ":" + str(SERVER_PORT)

# ---- 本地频道表(层级 -> 频道) ----
channels = []
func channel_of(layer_name) {
    i = 0
    while i < len(channels) {
        parts = split(channels[i], "|")
        if parts[0] == layer_name {
            return parts[1]
        }
        i = i + 1
    }
    return "verse://" + SERVER_HOST + ":" + str(SERVER_PORT) + "/ch/" + layer_name
}

# ---- 发言到层级频道 ----
func channel_say(layer_name, who, text) {
    ch = channel_of(layer_name)
    net_send(ch, who + "> " + text)
}

# ---- 监听当前层级频道 ----
func channel_listen(layer_name) {
    ch = channel_of(layer_name)
    return net_recv(ch)
}

# ---- 广播到所有频道(系统公告) ----
func channel_broadcast(text) {
    i = 0
    while i < len(channels) {
        parts = split(channels[i], "|")
        net_send(parts[1], "[公告] " + text)
        i = i + 1
    }
}

# ---- 测试 ----
say "net.im loaded: " + SERVER_ID + " 频道示例: " + channel_of("lobby")