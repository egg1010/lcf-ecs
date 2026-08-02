/* app.js - 文档主应用 (路由/侧边栏/搜索/渲染/粒子) */

(function () {
  'use strict';

  // === 分类元数据 ===
  var CATEGORIES = [
    { id: 'core',       name: 'ECS 核心',    icon: 'E' },
    { id: 'config',     name: '宏配置',      icon: '#' },
    { id: 'containers', name: '容器',        icon: 'C' },
    { id: 'allocators', name: '内存分配器',  icon: 'A' },
    { id: 'algorithms', name: '算法',        icon: 'S' },
    { id: 'tools',      name: '工具',        icon: 'T' },
    { id: 'utf8',       name: 'UTF-8 模块',  icon: 'U' }
  ];

  var DOM = {};
  var allModules = [];
  var currentModule = null;

  // === 初始化 ===
  function init() {
    cacheDom();
    buildModuleList();
    renderSidebar();
    renderHero();
    bindEvents();
    initParticles();
    route();
    window.addEventListener('hashchange', route);
  }

  function cacheDom() {
    DOM.sidebar      = document.querySelector('.sidebar');
    DOM.main         = document.querySelector('.main');
    DOM.content      = document.querySelector('.content');
    DOM.searchInput  = document.querySelector('.search-input');
    DOM.searchResults= document.querySelector('.search-results');
    DOM.backToTop    = document.querySelector('.back-to-top');
    DOM.themeToggle  = document.querySelector('.theme-toggle');
    DOM.mobileMenuBtn= document.querySelector('.mobile-menu-btn');
  }

  // === 构建模块列表 (从 DOCS_DATA) ===
  function buildModuleList() {
    var data = window.DOCS_DATA || {};
    var keys = Object.keys(data);
    keys.forEach(function (k) {
      allModules.push(data[k]);
    });
    allModules.sort(function (a, b) { return a.order - b.order; });
  }

  // === 渲染侧边栏 ===
  function renderSidebar() {
    var html = '';
    CATEGORIES.forEach(function (cat) {
      var mods = allModules.filter(function (m) { return m.category === cat.id; });
      if (mods.length === 0) return;

      html += '<div class="sidebar-section cat-' + cat.id + '">';
      html += '<div class="sidebar-cat-label" data-cat="' + cat.id + '">';
      html += '<span class="dot"></span>';
      html += '<span>' + cat.name + '</span>';
      html += '<span class="chevron">▼</span>';
      html += '</div>';
      html += '<div class="sidebar-items" data-cat-items="' + cat.id + '">';
      mods.forEach(function (m) {
        html += '<div class="sidebar-item" data-module="' + m.id + '">';
        html += '<span class="item-icon">' + m.icon + '</span>';
        html += '<span class="item-title">' + escapeText(m.title) + '</span>';
        html += '</div>';
      });
      html += '</div>';
      html += '</div>';
    });
    DOM.sidebar.innerHTML = html;
  }

  // === 渲染首页 ===
  function renderHero() {
    var total = allModules.length;
    var html = '';

    html += '<div class="hero page-transition">';
    html += '<h1>LCF-ECS 接口文档</h1>';
    html += '<p>C++20 · ' + total + ' 个模块</p>';
    html += '</div>';

    // 统计卡片
    html += '<div class="stats-grid">';
    html += statCard(total, '模块总数');
    html += statCard(CATEGORIES.length, '分类');
    html += statCard('C++20', '标准');
    html += statCard('0', '外部依赖');
    html += '</div>';

    // 仓库入口
    html += '<h2>仓库</h2>';
    html += '<div class="repo-card">';
    html += '<a class="repo-link" href="https://github.com/egg1010/lcf-ecs" target="_blank" rel="noopener">';
    html += '<svg width="20" height="20" viewBox="0 0 24 24" fill="currentColor"><path d="M12 0C5.37 0 0 5.37 0 12c0 5.31 3.435 9.795 8.205 11.385.6.105.825-.255.825-.57 0-.285-.015-1.23-.015-2.235-3.015.555-3.795-.735-4.035-1.41-.135-.345-.72-1.41-1.23-1.695-.42-.225-1.02-.78-.015-.795.945-.015 1.62.87 1.845 1.23 1.08 1.815 2.805 1.305 3.495.99.105-.78.42-1.305.765-1.605-2.67-.3-5.46-1.335-5.46-5.925 0-1.305.465-2.385 1.23-3.225-.12-.3-.54-1.53.12-3.18 0 0 1.005-.315 3.3 1.23.96-.27 1.98-.405 3-.405s2.04.135 3 .405c2.295-1.56 3.3-1.23 3.3-1.23.66 1.65.24 2.88.12 3.18.765.84 1.23 1.905 1.23 3.225 0 4.605-2.805 5.625-5.475 5.925.435.375.81 1.095.81 2.22 0 1.605-.015 2.895-.015 3.3 0 .315.225.69.825.57A12.02 12.02 0 0024 12c0-6.63-5.37-12-12-12z"/></svg>';
    html += '<span>github.com/egg1010/lcf-ecs</span>';
    html += '</a>';
    html += '</div>';

    // 分类入口
    html += '<h2>模块分类</h2>';
    html += '<div class="cat-grid">';
    CATEGORIES.forEach(function (cat) {
      var count = allModules.filter(function (m) { return m.category === cat.id; }).length;
      html += '<div class="cat-card cat-' + cat.id + '" data-cat-link="' + cat.id + '">';
      html += '<div class="cat-icon">' + cat.icon + '</div>';
      html += '<div class="cat-name">' + cat.name + '</div>';
      html += '<div class="cat-count">' + count + ' 个模块</div>';
      html += '</div>';
    });
    html += '</div>';

    DOM.content.innerHTML = html;

    // 绑定分类卡片点击
    document.querySelectorAll('[data-cat-link]').forEach(function (el) {
      el.addEventListener('click', function () {
        var cat = el.getAttribute('data-cat-link');
        // 展开对应分类并跳转到第一个模块
        var first = allModules.find(function (m) { return m.category === cat; });
        if (first) location.hash = '#/' + first.id;
      });
    });
  }

  function statCard(num, label) {
    return '<div class="stat-card"><div class="num">' + num + '</div><div class="label">' + label + '</div></div>';
  }

  // === 渲染模块内容 ===
  function renderModule(mod) {
    currentModule = mod;
    var cat = CATEGORIES.find(function (c) { return c.id === mod.category; });

    var html = '';
    html += '<div class="breadcrumb cat-' + mod.category + '">';
    html += '<span>' + (cat ? cat.name : mod.category) + '</span>';
    html += '<span class="sep">/</span>';
    html += '<span class="current">' + escapeText(mod.title) + '</span>';
    html += '<button class="btn home-btn" title="返回首页">';
    html += '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="m3 9 9-7 9 7v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/><polyline points="9 22 9 12 15 12 15 22"/></svg>';
    html += '<span>返回首页</span>';
    html += '</button>';
    html += '</div>';

    html += '<div class="doc-content page-transition cat-' + mod.category + '">';
    html += window.MD.parse(mod.content);
    html += '</div>';

    DOM.content.innerHTML = html;
    DOM.main.scrollTop = 0;

    // 返回首页按钮
    var homeBtn = DOM.content.querySelector('.home-btn');
    if (homeBtn) {
      homeBtn.addEventListener('click', function () {
        location.hash = '';
      });
    }

    // 更新侧边栏高亮
    updateSidebarActive();
  }

  function updateSidebarActive() {
    document.querySelectorAll('.sidebar-item').forEach(function (el) {
      el.classList.toggle('active', el.getAttribute('data-module') === (currentModule ? currentModule.id : ''));
    });
    // 展开当前模块所在分类
    if (currentModule) {
      var items = document.querySelector('[data-cat-items="' + currentModule.category + '"]');
      var label = document.querySelector('[data-cat="' + currentModule.category + '"]');
      if (items) items.classList.remove('collapsed');
      if (label) label.classList.remove('collapsed');
    }
  }

  // === 路由 ===
  function route() {
    var hash = location.hash.slice(2); // 去掉 #/
    if (!hash) {
      renderHero();
      currentModule = null;
      updateSidebarActive();
      return;
    }
    var mod = window.DOCS_DATA && window.DOCS_DATA[hash];
    if (mod) {
      renderModule(mod);
    } else {
      renderHero();
    }
  }

  // === 搜索 ===
  var searchTimer = null;
  function positionSearchResults() {
    var rect = DOM.searchInput.getBoundingClientRect();
    DOM.searchResults.style.top = (rect.bottom + 4) + 'px';
    DOM.searchResults.style.left = rect.left + 'px';
    DOM.searchResults.style.width = rect.width + 'px';
  }

  function onSearch() {
    var q = DOM.searchInput.value.trim().toLowerCase();
    if (!q) {
      DOM.searchResults.classList.remove('show');
      return;
    }

    positionSearchResults();
    clearTimeout(searchTimer);
    searchTimer = setTimeout(function () {
      var results = [];
      allModules.forEach(function (m) {
        var titleMatch = m.title.toLowerCase().indexOf(q);
        var contentMatch = m.content.toLowerCase().indexOf(q);

        if (titleMatch >= 0 || contentMatch >= 0) {
          var snippet = '';
          if (contentMatch >= 0) {
            var start = Math.max(0, contentMatch - 40);
            snippet = (start > 0 ? '...' : '') + m.content.substr(start, 100).replace(/\n/g, ' ') + '...';
          }
          results.push({ mod: m, titleMatch: titleMatch, snippet: snippet });
        }
      });

      // 标题匹配优先
      results.sort(function (a, b) {
        if (a.titleMatch >= 0 && b.titleMatch < 0) return -1;
        if (a.titleMatch < 0 && b.titleMatch >= 0) return 1;
        return a.mod.order - b.mod.order;
      });

      renderSearchResults(results, q);
    }, 150);
  }

  function renderSearchResults(results, q) {
    positionSearchResults();
    if (results.length === 0) {
      DOM.searchResults.innerHTML = '<div class="search-empty">未找到匹配的模块</div>';
      DOM.searchResults.classList.add('show');
      return;
    }

    var html = '';
    results.slice(0, 20).forEach(function (r) {
      var title = highlight(r.mod.title, q);
      var snippet = r.snippet ? highlight(r.snippet, q) : '';
      html += '<div class="search-result-item" data-module="' + r.mod.id + '">';
      html += '<div class="search-result-title">' + title + '</div>';
      if (snippet) html += '<div class="search-result-snippet">' + snippet + '</div>';
      html += '</div>';
    });

    DOM.searchResults.innerHTML = html;
    DOM.searchResults.classList.add('show');

    DOM.searchResults.querySelectorAll('.search-result-item').forEach(function (el) {
      el.addEventListener('click', function () {
        var id = el.getAttribute('data-module');
        location.hash = '#/' + id;
        DOM.searchInput.value = '';
        DOM.searchResults.classList.remove('show');
      });
    });
  }

  function highlight(text, q) {
    var escaped = escapeText(text);
    var re = new RegExp('(' + q.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')', 'gi');
    return escaped.replace(re, '<mark>$1</mark>');
  }

  // === 事件绑定 ===
  function bindEvents() {
    // 侧边栏点击
    DOM.sidebar.addEventListener('click', function (e) {
      var item = e.target.closest('.sidebar-item');
      if (item) {
        var id = item.getAttribute('data-module');
        location.hash = '#/' + id;
        // 移动端关闭侧边栏
        DOM.sidebar.classList.remove('open');
        return;
      }
      var label = e.target.closest('.sidebar-cat-label');
      if (label) {
        var cat = label.getAttribute('data-cat');
        var items = document.querySelector('[data-cat-items="' + cat + '"]');
        if (items) items.classList.toggle('collapsed');
        label.classList.toggle('collapsed');
      }
    });

    // 搜索
    DOM.searchInput.addEventListener('input', onSearch);
    DOM.searchInput.addEventListener('focus', onSearch);
    document.addEventListener('click', function (e) {
      if (!e.target.closest('.search-box')) {
        DOM.searchResults.classList.remove('show');
      }
    });

    // 滚动/缩放时隐藏搜索结果（fixed 定位会错位）
    window.addEventListener('scroll', function () {
      if (DOM.searchResults.classList.contains('show')) {
        DOM.searchResults.classList.remove('show');
      }
    }, true);
    window.addEventListener('resize', function () {
      if (DOM.searchResults.classList.contains('show')) {
        DOM.searchResults.classList.remove('show');
      }
    });

    // 回到顶部
    DOM.main.addEventListener('scroll', function () {
      DOM.backToTop.classList.toggle('show', DOM.main.scrollTop > 300);
      if (DOM.searchResults.classList.contains('show')) {
        DOM.searchResults.classList.remove('show');
      }
    });
    DOM.backToTop.addEventListener('click', function () {
      DOM.main.scrollTo({ top: 0, behavior: 'smooth' });
    });

    // 主题切换
    if (DOM.themeToggle) {
      DOM.themeToggle.addEventListener('click', function () {
        var cur = document.body.getAttribute('data-theme');
        document.body.setAttribute('data-theme', cur === 'light' ? '' : 'light');
        DOM.themeToggle.textContent = cur === 'light' ? '🌙' : '☀️';
      });
    }

    // 移动端菜单
    if (DOM.mobileMenuBtn) {
      DOM.mobileMenuBtn.addEventListener('click', function () {
        DOM.sidebar.classList.toggle('open');
      });
    }

    // 键盘快捷键: / 聚焦搜索
    document.addEventListener('keydown', function (e) {
      if (e.key === '/' && document.activeElement !== DOM.searchInput) {
        e.preventDefault();
        DOM.searchInput.focus();
      }
      if (e.key === 'Escape') {
        DOM.searchResults.classList.remove('show');
        DOM.searchInput.blur();
      }
    });
  }

  // === 粒子背景 ===
  function initParticles() {
    var bg = document.querySelector('.particle-bg');
    if (!bg) return;
    var count = 30;
    for (var i = 0; i < count; i++) {
      var p = document.createElement('div');
      p.className = 'particle';
      p.style.left = Math.random() * 100 + '%';
      p.style.animationDuration = (15 + Math.random() * 15) + 's';
      p.style.animationDelay = (Math.random() * 15) + 's';
      var colors = ['#00e5ff', '#4d7cff', '#a855f7', '#ec4899'];
      p.style.background = colors[i % colors.length];
      p.style.boxShadow = '0 0 4px ' + colors[i % colors.length];
      bg.appendChild(p);
    }
  }

  // === 工具 ===
  function escapeText(s) {
    var div = document.createElement('div');
    div.textContent = s;
    return div.innerHTML;
  }

  // 启动
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
