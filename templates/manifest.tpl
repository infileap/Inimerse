{
    "name": "PROJECT_NAME",
    "version": "0.1.0",
    "engine": "inimerse >= 1.0",
    "entry": "main.im",
    "layers": {
        "lobby": "游戏大厅(层级频道: lobby)",
        "game": "小游戏(临时层级, 进入时创建, 返回时销毁)"
    },
    "network": {
        "hub": "hub.example.com:8080",
        "channel_model": "layer-based (每个层级一个频道)"
    },
    "assets": [],
    "scripts": ["main.im", "net.im"]
}