// 站点配置 —— 只有这一个文件需要改就能换站名 / 仓库地址
window.INFIVERSE_SITE = {
  name: "Infiverse",
  tagline: "Inimerse 引擎与桌面客户端",
  // 内容以仓库为准：域名是免费二级域名，可能被回收，仓库里的才是 canonical。
  repoUrl: "https://github.com/infileap/Inimerse",
  releasesUrl: "https://github.com/infileap/Inimerse/releases",
  issuesUrl: "https://github.com/infileap/Inimerse/issues",
  // B站 账号空间地址，形如 "https://space.bilibili.com/<uid>"。
  // 留空 = 视频页不显示「B站 账号」按钮。**不要在这里编一个 UID**：
  // 编错的数字会把访客送到别人的主页，比没有按钮糟得多。
  bilibiliUrl: ""
};
