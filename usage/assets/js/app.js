/* app.js - 文档主应用 (路由/侧边栏/搜索/渲染/粒子) */

(function () {
  'use strict';

  // === 三层导航: 库接口 → 宏配置 → 模块接口 ===
  var BANDS = [
    { id: 'api',     name: '库接口' },
    { id: 'config',  name: '宏配置' },
    { id: 'modules', name: '模块接口' }
  ];

  var CATEGORIES = [
    { id: 'ecs',           name: 'ECS 核心',    icon: 'E', band: 'api',     entry: 'ecs/component.hpp' },
    { id: 'reflection',    name: '反射',        icon: 'R', band: 'api',     entry: 'reflection/reflection.hpp' },
    { id: 'serialization', name: '序列化',      icon: 'Z', band: 'api',     entry: 'serialization/serialization.hpp' },
    { id: 'thread',        name: '多线程',      icon: 'X', band: 'api',     entry: 'thread/thread_all.hpp' },
    { id: 'config',        name: '宏配置',      icon: '#', band: 'config' },
    { id: 'containers',    name: '容器',        icon: 'C', band: 'modules' },
    { id: 'allocators',    name: '内存分配器',  icon: 'A', band: 'modules' },
    { id: 'algorithms',    name: '算法',        icon: 'S', band: 'modules' },
    { id: 'codec',         name: '编解码',      icon: 'D', band: 'modules' },
    { id: 'time',          name: '时间',        icon: 'T', band: 'modules' },
    { id: 'type',          name: '类型',        icon: 'Y', band: 'modules' },
    { id: 'utf8',          name: 'UTF-8 模块',  icon: 'U', band: 'modules' },
    { id: 'tools',         name: '通用工具',    icon: 'K', band: 'modules' }
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
    Object.keys(data).forEach(function (k) {
      allModules.push(data[k]);
    });
    allModules.sort(function (a, b) { return a.order - b.order; });
  }

  function modulesOf(catId) {
    return allModules.filter(function (m) { return m.category === catId; });
  }

  // === 渲染侧边栏: 库接口 / 宏配置 / 模块接口 ===
  function renderSidebar() {
    var html = '';
    BANDS.forEach(function (band) {
      var cats = CATEGORIES.filter(function (c) { return c.band === band.id; });
      var hasItem = cats.some(function (c) { return modulesOf(c.id).length > 0; });
      if (!hasItem) return;

      html += '<div class="sidebar-band">' + band.name + '</div>';

      // 单分类分组直接平铺条目，避免“宏配置 / 宏配置 / 可选宏配置”三层重复
      if (cats.length === 1) {
        modulesOf(cats[0].id).forEach(function (m) {
          html += '<div class="sidebar-item" data-module="' + m.id + '">';
          html += '<span class="item-icon">' + m.icon + '</span>';
          html += '<span class="item-title">' + escapeText(m.title) + '</span>';
          html += '</div>';
        });
        return;
      }

      cats.forEach(function (cat) {
        var mods = modulesOf(cat.id);
        if (mods.length === 0) return;

        html += '<div class="sidebar-section cat-' + cat.id + '">';
        html += '<div class="sidebar-cat-label collapsed" data-cat="' + cat.id + '">';
        html += '<span class="dot"></span>';
        html += '<span>' + cat.name + '</span>';
        html += '<span class="chevron">▼</span>';
        html += '</div>';
        html += '<div class="sidebar-items collapsed" data-cat-items="' + cat.id + '">';
        mods.forEach(function (m) {
          html += '<div class="sidebar-item" data-module="' + m.id + '">';
          html += '<span class="item-icon">' + m.icon + '</span>';
          html += '<span class="item-title">' + escapeText(m.title) + '</span>';
          html += '</div>';
        });
        html += '</div>';
        html += '</div>';
      });
    });
    DOM.sidebar.innerHTML = html;
  }

  // === 渲染首页: 上库接口 / 中宏配置 / 下模块接口 ===
  function renderHero() {
    var total = allModules.length;
    var html = '';

    html += '<div class="hero page-transition">';
    html += '<h1>LCF-ECS 接口文档</h1>';
    html += '<p>C++20 · ' + total + ' 个模块 · 零外部依赖</p>';
    html += '</div>';

    html += '<div class="stats-grid">';
    html += statCard(total, '模块总数');
    html += statCard(CATEGORIES.length, '分类');
    html += statCard('C++20', '标准');
    html += statCard('0', '外部依赖');
    html += '</div>';

    // 上部: 库接口
    html += '<h2>库接口</h2>';
    html += '<div class="cat-grid api-grid">';
    CATEGORIES.filter(function (c) { return c.band === 'api'; }).forEach(function (cat) {
      html += catCard(cat, true);
    });
    html += '</div>';

    // 中部: 宏配置
    html += '<h2>宏配置</h2>';
    html += '<div class="cat-grid">';
    CATEGORIES.filter(function (c) { return c.band === 'config'; }).forEach(function (cat) {
      html += catCard(cat, false);
    });
    html += '</div>';

    // 下部: 模块接口
    html += '<h2>模块接口</h2>';
    html += '<div class="cat-grid">';
    CATEGORIES.filter(function (c) { return c.band === 'modules'; }).forEach(function (cat) {
      html += catCard(cat, false);
    });
    html += '</div>';

    DOM.content.innerHTML = html;

    document.querySelectorAll('[data-cat-link]').forEach(function (el) {
      el.addEventListener('click', function () {
        var cat = el.getAttribute('data-cat-link');
        var first = allModules.find(function (m) { return m.category === cat; });
        if (first) location.hash = '#/' + first.id;
      });
    });
  }

  function catCard(cat, withEntry) {
    var count = modulesOf(cat.id).length;
    var unit = cat.band === 'config' ? ' 页' : ' 个模块';
    var html = '';
    html += '<div class="cat-card cat-' + cat.id + '" data-cat-link="' + cat.id + '">';
    html += '<div class="cat-icon">' + cat.icon + '</div>';
    html += '<div class="cat-name">' + cat.name + '</div>';
    html += '<div class="cat-count">' + count + unit + '</div>';
    if (withEntry && cat.entry) {
      html += '<div class="cat-entry">' + cat.entry + '</div>';
    }
    html += '</div>';
    return html;
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

    var homeBtn = DOM.content.querySelector('.home-btn');
    if (homeBtn) {
      homeBtn.addEventListener('click', function () {
        location.hash = '';
      });
    }

    updateSidebarActive();
  }

  function updateSidebarActive() {
    document.querySelectorAll('.sidebar-item').forEach(function (el) {
      el.classList.toggle('active', el.getAttribute('data-module') === (currentModule ? currentModule.id : ''));
    });
    if (currentModule) {
      var items = document.querySelector('[data-cat-items="' + currentModule.category + '"]');
      var label = document.querySelector('[data-cat="' + currentModule.category + '"]');
      if (items) items.classList.remove('collapsed');
      if (label) label.classList.remove('collapsed');
    }
  }

  // === 路由 ===
  function route() {
    var hash = location.hash.slice(2);
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
    DOM.sidebar.addEventListener('click', function (e) {
      var item = e.target.closest('.sidebar-item');
      if (item) {
        var id = item.getAttribute('data-module');
        location.hash = '#/' + id;
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

    DOM.searchInput.addEventListener('input', onSearch);
    DOM.searchInput.addEventListener('focus', onSearch);
    document.addEventListener('click', function (e) {
      if (!e.target.closest('.search-box')) {
        DOM.searchResults.classList.remove('show');
      }
    });

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

    DOM.main.addEventListener('scroll', function () {
      DOM.backToTop.classList.toggle('show', DOM.main.scrollTop > 300);
      if (DOM.searchResults.classList.contains('show')) {
        DOM.searchResults.classList.remove('show');
      }
    });
    DOM.backToTop.addEventListener('click', function () {
      DOM.main.scrollTo({ top: 0, behavior: 'smooth' });
    });

    if (DOM.themeToggle) {
      DOM.themeToggle.addEventListener('click', function () {
        var cur = document.body.getAttribute('data-theme');
        document.body.setAttribute('data-theme', cur === 'light' ? '' : 'light');
        DOM.themeToggle.textContent = cur === 'light' ? '🌙' : '☀️';
      });
    }

    if (DOM.mobileMenuBtn) {
      DOM.mobileMenuBtn.addEventListener('click', function () {
        DOM.sidebar.classList.toggle('open');
      });
    }

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

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
