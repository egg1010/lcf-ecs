/* md.js - 轻量 Markdown 解析器 (零依赖, 离线可用) */

(function (global) {
  'use strict';

  function escapeHtml(s) {
    return s
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#39;');
  }

  function inline(text) {
    // 行内代码 (先处理, 避免内部被其他规则匹配)
    var codes = [];
    text = text.replace(/`([^`]+)`/g, function (_, c) {
      codes.push(c);
      return '\x00CODE' + (codes.length - 1) + '\x00';
    });

    text = escapeHtml(text);

    // 粗体 + 斜体
    text = text.replace(/\*\*\*(.+?)\*\*\*/g, '<strong><em>$1</em></strong>');
    text = text.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');
    text = text.replace(/(?<!\w)\*(?!\s)(.+?)(?<!\s)\*(?!\w)/g, '<em>$1</em>');

    // 删除线
    text = text.replace(/~~(.+?)~~/g, '<del>$1</del>');

    // 链接
    text = text.replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<a href="$2" target="_blank" rel="noopener">$1</a>');

    // 还原行内代码
    text = text.replace(/\x00CODE(\d+)\x00/g, function (_, i) {
      return '<code>' + escapeHtml(codes[+i]) + '</code>';
    });

    return text;
  }

  function parseTable(lines, i) {
    var header = lines[i].trim();
    if (!header.startsWith('|')) return null;
    var sep = lines[i + 1] ? lines[i + 1].trim() : '';
    if (!/^\|[\s\-:|]+\|$/.test(sep)) return null;

    var cells = header.split('|').filter(function (c) { return c.trim() !== ''; });
    var html = '<table><thead><tr>';
    cells.forEach(function (c) { html += '<th>' + inline(c.trim()) + '</th>'; });
    html += '</tr></thead><tbody>';

    var j = i + 2;
    while (j < lines.length && lines[j].trim().startsWith('|')) {
      var row = lines[j].trim().split('|').filter(function (c) { return c.trim() !== '' || c === ''; });
      // 处理空单元格
      var raw = lines[j].trim();
      if (raw.startsWith('|')) raw = raw.slice(1);
      if (raw.endsWith('|')) raw = raw.slice(0, -1);
      row = raw.split('|');
      html += '<tr>';
      row.forEach(function (c) { html += '<td>' + inline(c.trim()) + '</td>'; });
      html += '</tr>';
      j++;
    }
    html += '</tbody></table>';
    return { html: html, next: j };
  }

  // C++ 关键字 / 类型 / 字面量 / 注释的高亮
  var CPP_KW = new Set([
    'alignas','alignof','and','and_eq','asm','auto','bitand','bitor','bool',
    'break','case','catch','char','char8_t','char16_t','char32_t','class','compl',
    'concept','const','consteval','constexpr','constinit','const_cast','continue',
    'co_await','co_return','co_yield','decltype','default','delete','do','double',
    'dynamic_cast','else','enum','explicit','export','extern','false','final','float',
    'for','friend','goto','if','inline','int','long','mutable','namespace','new','noexcept',
    'not','not_eq','nullptr','operator','or','or_eq','override','private','protected',
    'public','register','reinterpret_cast','requires','return','short','signed','sizeof',
    'static','static_assert','static_cast','struct','switch','template','this',
    'thread_local','throw','true','try','typedef','typeid','typename','union','unsigned',
    'using','virtual','void','volatile','wchar_t','while','xor','xor_eq'
  ]);

  function highlightCpp(code) {
    // 用 Unicode Private Use Area 字符作占位符
    // 占位符格式: \uE000 + 索引 + \uE001
    // 索引数字在占位符内部，需在后续正则中用负向断言排除
    var placeholders = [];

    function store(html) {
      placeholders.push(html);
      return '\uE000' + (placeholders.length - 1) + '\uE001';
    }

    // 预处理指令 (#include / #define / #ifdef ...)
    code = code.replace(/^[ \t]*(#[a-zA-Z_]+)\b/gm, function (m) {
      return store('<span class="tok-pre">' + escapeHtml(m) + '</span>');
    });

    // 单行注释 //
    code = code.replace(/\/\/[^\n]*/g, function (m) {
      return store('<span class="tok-cmt">' + escapeHtml(m) + '</span>');
    });

    // 多行注释 /* */
    code = code.replace(/\/\*[\s\S]*?\*\//g, function (m) {
      return store('<span class="tok-cmt">' + escapeHtml(m) + '</span>');
    });

    // 字符串 "..." (含转义)
    code = code.replace(/"(?:\\.|[^"\\\n])*"/g, function (m) {
      return store('<span class="tok-str">' + escapeHtml(m) + '</span>');
    });

    // 字符 '...'
    code = code.replace(/'(?:\\.|[^'\\\n])*'/g, function (m) {
      return store('<span class="tok-str">' + escapeHtml(m) + '</span>');
    });

    // 转义剩余 HTML（\uE000/\uE001 不会被转义）
    code = escapeHtml(code);

    // 数字字面量（用负向断言排除占位符 \uE000<数字>\uE001 内的索引）
    code = code.replace(/(?<!\uE000)\b(0[xX][0-9a-fA-F]+|[0-9]+\.?[0-9]*[eE]?[+-]?[0-9]*[fFuUlL]*)\b(?!\uE001)/g, function (m) {
      return store('<span class="tok-num">' + m + '</span>');
    });

    // 标识符 → 关键字 / 类型高亮（占位符 \uE000 不会被 \b 匹配）
    code = code.replace(/\b([a-zA-Z_]\w*)\b/g, function (m, name) {
      if (CPP_KW.has(name)) {
        return '<span class="tok-kw">' + name + '</span>';
      }
      // 大写开头 或 含 _t 后缀的常见类型
      if (/^[A-Z]/.test(name) || /_t$/.test(name)) {
        return '<span class="tok-type">' + name + '</span>';
      }
      return m;
    });

    // 还原占位符
    code = code.replace(/\uE000(\d+)\uE001/g, function (_, i) {
      return placeholders[+i];
    });

    return code;
  }

  function parse(md) {
    var lines = md.split('\n');
    var html = '';
    var i = 0;

    while (i < lines.length) {
      var line = lines[i];

      // 空行
      if (line.trim() === '') { i++; continue; }

      // 代码块
      if (line.trim().startsWith('```')) {
        var lang = line.trim().slice(3).trim();
        var code = '';
        i++;
        while (i < lines.length && !lines[i].trim().startsWith('```')) {
          code += lines[i] + '\n';
          i++;
        }
        i++;
        var rendered = (lang === 'cpp' || lang === 'c++' || lang === '' || lang === 'c')
          ? highlightCpp(code.replace(/\n$/, ''))
          : escapeHtml(code.replace(/\n$/, ''));
        html += '<pre><code class="lang-' + (lang || 'cpp') + '">' + rendered + '</code></pre>';
        continue;
      }

      // 标题
      var h = line.match(/^(#{1,6})\s+(.*)/);
      if (h) {
        var level = h[1].length;
        html += '<h' + level + '>' + inline(h[2]) + '</h' + level + '>';
        i++;
        continue;
      }

      // 分割线
      if (/^(-{3,}|\*{3,}|_{3,})\s*$/.test(line)) {
        html += '<hr>';
        i++;
        continue;
      }

      // 引用块
      if (line.trim().startsWith('>')) {
        var quote = '';
        while (i < lines.length && lines[i].trim().startsWith('>')) {
          quote += lines[i].trim().replace(/^>\s?/, '') + '\n';
          i++;
        }
        html += '<blockquote>' + inline(quote.trim()) + '</blockquote>';
        continue;
      }

      // 表格
      if (line.trim().startsWith('|')) {
        var tbl = parseTable(lines, i);
        if (tbl) {
          html += tbl.html;
          i = tbl.next;
          continue;
        }
      }

      // 无序列表
      if (/^[\s]*[-*+]\s+/.test(line)) {
        var ul = '';
        while (i < lines.length && /^[\s]*[-*+]\s+/.test(lines[i])) {
          ul += '<li>' + inline(lines[i].replace(/^[\s]*[-*+]\s+/, '')) + '</li>';
          i++;
        }
        html += '<ul>' + ul + '</ul>';
        continue;
      }

      // 有序列表
      if (/^[\s]*\d+\.\s+/.test(line)) {
        var ol = '';
        while (i < lines.length && /^[\s]*\d+\.\s+/.test(lines[i])) {
          ol += '<li>' + inline(lines[i].replace(/^[\s]*\d+\.\s+/, '')) + '</li>';
          i++;
        }
        html += '<ol>' + ol + '</ol>';
        continue;
      }

      // 段落 (连续非空行)
      var para = line;
      i++;
      while (i < lines.length && lines[i].trim() !== '' &&
        !lines[i].match(/^(#{1,6})\s/) &&
        !lines[i].trim().startsWith('```') &&
        !lines[i].trim().startsWith('|') &&
        !/^[\s]*[-*+]\s+/.test(lines[i]) &&
        !/^[\s]*\d+\.\s+/.test(lines[i]) &&
        !lines[i].trim().startsWith('>') &&
        !/^(-{3,}|\*{3,}|_{3,})\s*$/.test(lines[i])) {
        para += ' ' + lines[i];
        i++;
      }
      html += '<p>' + inline(para) + '</p>';
    }

    return html;
  }

  global.MD = { parse: parse, escape: escapeHtml, inline: inline };
})(window);
