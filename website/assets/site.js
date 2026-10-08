/* 站点渲染 —— 经典脚本，无模块、无 fetch、无依赖。
   这样 file:// 双击打开、静态托管、整包上传到 B站 Toy 三种场景行为一致。 */
(function () {
  'use strict';

  var BVID_RE = /^BV[0-9A-Za-z]{8,12}$/;

  /* ---------- 站点根：数据里的路径是「相对站点根」写的 ----------
     data/videos.js 里的 cover 写成 "assets/covers/x.jpg"。一份数据被首页和各子页共用，
     所以它没法写成 "../assets/..."（首页要的是没有 ../ 的那个）。
     而浏览器解析相对路径是按**页面目录**来的：从 /videos/ 打开会去找 /videos/assets/...
     ⇒ 404；整包传到 B站 Toy 的 /toy/<slug>/ 下同理，更糟。
     所以这里从本脚本自己的 URL 反推站点根 —— 页面嵌多深都对，且不需要每页写一个前缀。
     取不到时返回 ''（退回改动前的行为，不制造新故障）。 */
  function siteRoot() {
    try {
      var s = document.currentScript;
      if (!s || !s.src) {
        var all = document.getElementsByTagName('script');
        for (var i = all.length - 1; i >= 0; i--) {
          var u = all[i].src || '';
          if (/\/assets\/site\.js(\?|$)/.test(u)) { s = all[i]; break; }
        }
      }
      if (s && s.src) return s.src.replace(/assets\/site\.js(\?.*)?$/, '');
    } catch (e) { /* 取不到就退回旧行为 */ }
    return '';
  }
  var SITE_BASE = siteRoot();

  /** 站点根相对路径 → 浏览器真能取到的地址；外链与绝对路径原样返回 */
  function asset(p) {
    if (!p || /^[a-z][a-z0-9+.-]*:/i.test(p) || p.charAt(0) === '/') return p;
    return SITE_BASE + p;
  }

  function el(tag, cls, text) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    if (text != null) n.textContent = text;
    return n;
  }

  function isPublished(bvid) {
    return typeof bvid === 'string' && BVID_RE.test(bvid.trim());
  }

  /* ---------- 视频卡 ---------- */
  function videoCard(rec) {
    var portrait = rec.orientation === 'portrait';
    var card = el('article', 'video' + (rec.featured ? ' video--featured' : ''));

    var coverHref = isPublished(rec.bvid)
      ? 'https://www.bilibili.com/video/' + rec.bvid.trim() + '/'
      : null;

    var cover = el(coverHref ? 'a' : 'div',
      'video__cover' + (portrait ? ' video__cover--portrait' : ''));
    if (coverHref) {
      cover.href = coverHref;
      cover.target = '_blank';
      cover.rel = 'noopener';
    }

    if (rec.cover) {
      var img = document.createElement('img');
      img.src = asset(rec.cover);
      img.alt = rec.title || '';
      img.loading = 'lazy';
      cover.appendChild(img);
    } else {
      cover.appendChild(el('span', 'video__placeholder', '封面待交付'));
    }
    if (rec.duration) cover.appendChild(el('span', 'video__dur', rec.duration));
    card.appendChild(cover);

    var body = el('div', 'video__body');
    body.appendChild(el('h3', 'video__title', rec.title || '(无标题)'));
    if (rec.summary) body.appendChild(el('p', 'video__desc', rec.summary));

    var meta = el('p', 'video__meta');
    var parts = [];
    if (rec.resolution) parts.push(rec.resolution);
    parts.push(isPublished(rec.bvid) ? 'B站 ' + rec.bvid.trim() : 'B站 未投稿');
    if (rec.date) parts.push(rec.date);
    if (rec.ref) parts.push('ref ' + String(rec.ref).slice(0, 7));
    parts.forEach(function (p) { meta.appendChild(el('span', null, p)); });
    body.appendChild(meta);

    var actions = el('div', 'video__actions');
    if (coverHref) {
      var watch = el('a', 'btn btn--primary', '在 B站 观看');
      watch.href = coverHref; watch.target = '_blank'; watch.rel = 'noopener';
      actions.appendChild(watch);

      var embed = el('button', 'btn btn--ghost', '在站内播放');
      embed.type = 'button';
      embed.addEventListener('click', function () {
        var iframe = document.createElement('iframe');
        iframe.className = 'embed';
        iframe.src = 'https://player.bilibili.com/player.html?bvid=' +
          encodeURIComponent(rec.bvid.trim()) + '&page=1&autoplay=0';
        iframe.allowFullscreen = true;
        iframe.setAttribute('scrolling', 'no');
        iframe.setAttribute('frameborder', '0');
        iframe.title = rec.title || 'B站播放器';
        cover.replaceWith(iframe);
        actions.remove();
      });
      actions.appendChild(embed);
    } else {
      actions.appendChild(el('span', 'badge badge--partial', '待投稿后补 BV 号'));
    }
    body.appendChild(actions);

    card.appendChild(body);
    return card;
  }

  function renderVideos() {
    var host = document.getElementById('video-list');
    if (!host) return;
    var list = window.INFIVERSE_VIDEOS || [];
    host.textContent = '';
    if (!list.length) {
      var empty = el('div', 'empty');
      empty.appendChild(el('p', null, '还没有视频记录。'));
      empty.appendChild(el('p', null,
        '记录格式见 data/videos.js 顶部；没有记录的视频不在这里显示。'));
      host.appendChild(empty);
      return;
    }
    list.forEach(function (rec) { host.appendChild(videoCard(rec)); });
  }

  /* ---------- Toy / 在线作品卡 ---------- */
  function renderToys() {
    var host = document.getElementById('toy-list');
    if (!host) return;
    var list = window.INFIVERSE_TOYS || [];
    host.textContent = '';
    if (!list.length) {
      var empty = el('div', 'empty');
      empty.appendChild(el('p', null, '还没有已发布的作品。'));
      empty.appendChild(el('p', null,
        '发布权限需要 B站 Toy 内测资格（当前为邀请制）；拿到资格后用官方 toy CLI 提交。'));
      host.appendChild(empty);
      return;
    }
    list.forEach(function (t) {
      var card = el('a', 'card');
      card.href = t.url || '#';
      if (t.url && t.channel === 'toy') { card.target = '_blank'; card.rel = 'noopener'; }
      if (t.poster) {                       // 数据里的路径同样是「相对站点根」，走 asset()
        var poster = el('img', 'card__poster');
        poster.src = asset(t.poster);
        poster.alt = t.title || t.slug || '';
        card.appendChild(poster);
      }
      card.appendChild(el('h3', null, t.title || t.slug || '(无标题)'));
      if (t.summary) card.appendChild(el('p', null, t.summary));
      var badges = el('div', 'badges');
      badges.appendChild(el('span',
        'badge ' + (t.status === 'live' ? 'badge--live' : 'badge--partial'),
        t.status === 'live' ? '今天可玩' : '等前置：内测资格'));
      if (t.channel === 'toy') badges.appendChild(el('span', 'badge badge--muted', 'B站 Toy 托管'));
      card.appendChild(badges);
      host.appendChild(card);
    });
  }

  function ready(fn) {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', fn);
    } else { fn(); }
  }

  ready(function () {
    renderVideos();
    renderToys();
    var y = document.getElementById('year');
    if (y) y.textContent = String(new Date().getFullYear());

    /* B站 账号按钮：地址没填就不显示（见 data/site.js 的 bilibiliUrl）。 */
    var site = window.INFIVERSE_SITE || {};
    var bili = document.getElementById('bili-account');
    if (bili && site.bilibiliUrl) {
      bili.href = site.bilibiliUrl;
      bili.hidden = false;
    }
  });
})();
