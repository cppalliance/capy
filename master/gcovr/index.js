/* GCOVR Custom JavaScript - Tree View & Interactivity */

(function() {
  'use strict';

  // Wait for DOM ready
  document.addEventListener('DOMContentLoaded', function() {
    initTheme();
    initSidebar();
    initSidebarResize();
    initMobileMenu();
    initFileTree();
    initNavOverride();
    initBreadcrumbs();
    initSearch();
    initFunctionRows();
    initSorting();
    initToggleButtons();
    initCoverageNav();
    initTreeControls();
    initViewToggle();
    initSettingsDropdown();
    initTlaNavigation();
    initLineHighlight();
    initColumnToggles();
    initPopupResize();
    initFileNavTooltips();
    initFileNavKeys();
    initFunctionListPersistence();

    // Reveal page now that all init is done
    document.documentElement.classList.remove('no-transitions');

    // Prefetch linked pages on hover for instant navigation
    initPrefetch();
  });

  // ===========================================
  // Breadcrumb Links
  // ===========================================

  // Find a node in the tree by its link (HTML filename) and return
  // the full ancestor path as an array of nodes from root to target.
  function findPathInTree(nodes, targetLink) {
    for (var i = 0; i < nodes.length; i++) {
      var node = nodes[i];
      if (node.link === targetLink) {
        return [node];
      }
      if (node.children) {
        var childPath = findPathInTree(node.children, targetLink);
        if (childPath) {
          return [node].concat(childPath);
        }
      }
    }
    return null;
  }

  function initBreadcrumbs() {
    var currentSpan = document.querySelector('.breadcrumb .current');
    if (!currentSpan || !window.GCOVR_TREE_DATA) {
      if (currentSpan) currentSpan.classList.add('ready');
      return;
    }

    // Find current page in tree by its HTML filename — this is unambiguous
    // since each page only appears once in the tree.
    var currentPage = window.location.pathname.split('/').pop() || 'index.html';
    var treePath = findPathInTree(window.GCOVR_TREE_DATA, currentPage);

    if (!treePath || treePath.length === 0) {
      currentSpan.classList.add('ready');
      return;
    }

    // Build breadcrumb from the tree path (ancestor nodes → current node)
    var fragment = document.createDocumentFragment();
    var matchedSegments = [];

    // Fill an element with the segments of a (possibly joined) name like
    // "boost/url", rendering "boost", a separator, "url". Used so a joined
    // directory shows its segments inline yet remains one hyperlink target.
    function appendSegments(parentEl, name) {
      var segments = name.split('/');
      for (var k = 0; k < segments.length; k++) {
        if (k > 0) {
          var inner = document.createElement('span');
          inner.className = 'separator';
          inner.textContent = '/';
          parentEl.appendChild(inner);
        }
        parentEl.appendChild(document.createTextNode(segments[k]));
      }
    }

    for (var i = 0; i < treePath.length; i++) {
      var node = treePath[i];
      var isLast = (i === treePath.length - 1);

      if (i > 0) {
        var sep = document.createElement('span');
        sep.className = 'separator';
        sep.textContent = '/';
        fragment.appendChild(sep);
      }

      matchedSegments.push(node.name);

      if (node.link && !isLast) {
        var a = document.createElement('a');
        a.href = node.link;
        appendSegments(a, node.name);
        fragment.appendChild(a);
      } else {
        var span = document.createElement('span');
        span.className = 'current-file';
        appendSegments(span, node.name);
        fragment.appendChild(span);
      }
    }

    currentSpan.innerHTML = '';
    currentSpan.appendChild(fragment);
    currentSpan.classList.add('ready');

    // Update source-filename to match breadcrumb path
    var sourceFilename = document.querySelector('.source-filename');
    if (sourceFilename) {
      sourceFilename.textContent = matchedSegments.join('/');
    }
  }

  // ===========================================
  // Theme Toggle
  // ===========================================

  function initTheme() {
    const toggle = document.getElementById('theme-toggle');
    const iconSun = toggle ? toggle.querySelector('.icon-sun') : null;
    const iconMoon = toggle ? toggle.querySelector('.icon-moon') : null;

    // Get system preference
    function getSystemTheme() {
      return window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
    }

    // Get effective theme: saved preference or OS default
    function getEffectiveTheme() {
      var saved = localStorage.getItem('gcovr-theme');
      return (saved === 'light' || saved === 'dark') ? saved : getSystemTheme();
    }

    // Apply theme to document
    function applyTheme(theme) {
      document.documentElement.setAttribute('data-theme', theme);
      if (iconSun) iconSun.style.display = (theme === 'dark') ? 'block' : 'none';
      if (iconMoon) iconMoon.style.display = (theme === 'light') ? 'block' : 'none';
    }

    // Apply current theme
    applyTheme(getEffectiveTheme());

    // Listen for system theme changes — only apply if no stored preference
    window.matchMedia('(prefers-color-scheme: light)').addEventListener('change', function() {
      var saved = localStorage.getItem('gcovr-theme');
      if (saved !== 'light' && saved !== 'dark') {
        applyTheme(getSystemTheme());
      }
    });

    // Toggle between light and dark on click
    if (toggle) {
      toggle.addEventListener('click', function() {
        var current = getEffectiveTheme();
        var next = (current === 'dark') ? 'light' : 'dark';
        localStorage.setItem('gcovr-theme', next);
        applyTheme(next);
      });
    }
  }

  // ===========================================
  // Tree Controls (Expand/Collapse All)
  // ===========================================

  function initTreeControls() {
    var expandBtn = document.getElementById('expand-all');
    var collapseBtn = document.getElementById('collapse-all');

    if (expandBtn) {
      expandBtn.addEventListener('click', function() {
        document.querySelectorAll('.tree-item').forEach(function(item) {
          if (!item.classList.contains('no-children')) {
            item.classList.add('expanded');
            var toggle = item.querySelector(':scope > .tree-item-header > .tree-folder-toggle');
            if (toggle) toggle.textContent = '−';
          }
        });
        saveExpandedFolders();
      });
    }

    if (collapseBtn) {
      collapseBtn.addEventListener('click', function() {
        document.querySelectorAll('.tree-item').forEach(function(item) {
          item.classList.remove('expanded');
          var toggle = item.querySelector(':scope > .tree-item-header > .tree-folder-toggle');
          if (toggle) toggle.textContent = '+';
        });
        saveExpandedFolders();
      });
    }
  }

  // ===========================================
  // Sidebar Toggle
  // ===========================================

  function initSidebar() {
    const sidebar = document.getElementById('sidebar');
    const toggle = document.getElementById('sidebar-toggle');
    const header = sidebar ? sidebar.querySelector('.sidebar-header') : null;

    if (!sidebar) return;

    // Load saved state
    const isCollapsed = localStorage.getItem('sidebar-collapsed') === 'true';
    if (isCollapsed) {
      sidebar.classList.add('collapsed');
    }

    // Toggle button
    if (toggle) {
      toggle.addEventListener('click', function() {
        sidebar.classList.toggle('collapsed');
        sidebar.classList.remove('hover-expand');
        var isNowCollapsed = sidebar.classList.contains('collapsed');
        localStorage.setItem('sidebar-collapsed', isNowCollapsed);
        // Restore custom width when un-collapsing
        if (!isNowCollapsed) {
          var savedWidth = localStorage.getItem('gcovr-sidebar-width');
          if (savedWidth) {
            document.documentElement.style.setProperty('--sidebar-width', savedWidth + 'px');
          }
        }
      });
    }

    // Hover expand - expands when hovering sidebar content (not header or no-expand zones)
    var hoverTimeout = null;
    var HOVER_DELAY = 150; // ms delay before expanding
    var isOverContent = false;

    // Check if element is within a no-expand zone
    function isInNoExpandZone(el) {
      while (el && el !== sidebar) {
        if (el.classList && el.classList.contains('no-expand')) {
          return true;
        }
        el = el.parentElement;
      }
      return false;
    }

    function scheduleExpand() {
      if (hoverTimeout) return; // already scheduled
      if (sidebar.classList.contains('hover-expand')) return; // already expanded
      hoverTimeout = setTimeout(function() {
        if (isOverContent) {
          sidebar.classList.add('hover-expand');
        }
        hoverTimeout = null;
      }, HOVER_DELAY);
    }

    function cancelExpand() {
      if (hoverTimeout) {
        clearTimeout(hoverTimeout);
        hoverTimeout = null;
      }
      sidebar.classList.remove('hover-expand');
    }

    sidebar.addEventListener('mouseenter', function(e) {
      if (!sidebar.classList.contains('collapsed')) return;
      // Check if entering over content area (not header or no-expand zones)
      if (!header.contains(e.target) && !isInNoExpandZone(e.target)) {
        isOverContent = true;
        scheduleExpand();
      }
    });

    sidebar.addEventListener('mousemove', function(e) {
      if (!sidebar.classList.contains('collapsed')) return;
      var wasOverContent = isOverContent;
      isOverContent = !header.contains(e.target) && !isInNoExpandZone(e.target);

      if (isOverContent && !wasOverContent && !sidebar.classList.contains('hover-expand')) {
        scheduleExpand();
      }
    });

    sidebar.addEventListener('mouseleave', function() {
      isOverContent = false;
      cancelExpand();
    });
  }

  // ===========================================
  // Sidebar Resize
  // ===========================================

  function initSidebarResize() {
    var sidebar = document.getElementById('sidebar');
    var handle = document.getElementById('sidebar-resize-handle');
    if (!sidebar || !handle) return;

    var MIN_WIDTH = 200;
    var startX, startWidth;

    // Restore saved width
    var savedWidth = localStorage.getItem('gcovr-sidebar-width');
    if (savedWidth && !sidebar.classList.contains('collapsed')) {
      var w = parseInt(savedWidth, 10);
      if (w >= MIN_WIDTH) {
        document.documentElement.style.setProperty('--sidebar-width', w + 'px');
      }
    }

    function getMaxWidth() {
      return Math.floor(window.innerWidth * 0.5);
    }

    function onMouseMove(e) {
      var newWidth = startWidth + (e.clientX - startX);
      var maxW = getMaxWidth();
      if (newWidth < MIN_WIDTH) newWidth = MIN_WIDTH;
      if (newWidth > maxW) newWidth = maxW;
      document.documentElement.style.setProperty('--sidebar-width', newWidth + 'px');
    }

    function onMouseUp() {
      document.body.classList.remove('sidebar-resizing');
      document.removeEventListener('mousemove', onMouseMove);
      document.removeEventListener('mouseup', onMouseUp);
      // Save the current width
      var computed = parseInt(getComputedStyle(sidebar).width, 10);
      localStorage.setItem('gcovr-sidebar-width', computed);
    }

    handle.addEventListener('mousedown', function(e) {
      if (sidebar.classList.contains('collapsed')) return;
      e.preventDefault();
      startX = e.clientX;
      startWidth = parseInt(getComputedStyle(sidebar).width, 10);
      document.body.classList.add('sidebar-resizing');
      document.addEventListener('mousemove', onMouseMove);
      document.addEventListener('mouseup', onMouseUp);
    });

    // Double-click to reset to default width
    var DEFAULT_WIDTH = 320;
    handle.addEventListener('dblclick', function() {
      if (sidebar.classList.contains('collapsed')) return;
      document.documentElement.style.setProperty('--sidebar-width', DEFAULT_WIDTH + 'px');
      localStorage.setItem('gcovr-sidebar-width', DEFAULT_WIDTH);
    });
  }

  // ===========================================
  // Mobile Menu
  // ===========================================

  function initMobileMenu() {
    var sidebar = document.getElementById('sidebar');
    var menuBtn = document.getElementById('mobile-menu-btn');
    var backdrop = document.getElementById('sidebar-backdrop');

    if (!menuBtn || !sidebar) return;

    // Open sidebar on hamburger click
    menuBtn.addEventListener('click', function() {
      sidebar.classList.add('mobile-open');
    });

    // Close on backdrop click
    if (backdrop) {
      backdrop.addEventListener('click', function() {
        sidebar.classList.remove('mobile-open');
      });
    }

    // Close when clicking a navigation link
    sidebar.addEventListener('click', function(e) {
      if (e.target.closest('a[href]')) {
        sidebar.classList.remove('mobile-open');
      }
    });

    // Close on escape key
    document.addEventListener('keydown', function(e) {
      if (e.key === 'Escape' && sidebar.classList.contains('mobile-open')) {
        sidebar.classList.remove('mobile-open');
      }
    });
  }

  // ===========================================
  // File Tree - Load from tree.json
  // ===========================================

  function initFileTree() {
    var treeContainer = document.getElementById('file-tree');
    if (!treeContainer) return;

    renderTree(treeContainer, window.GCOVR_TREE_DATA);
  }

  // Save expanded folder paths to localStorage
  function saveExpandedFolders() {
    var paths = [];
    document.querySelectorAll('.tree-item.expanded[data-tree-path]').forEach(function(el) {
      paths.push(el.getAttribute('data-tree-path'));
    });
    localStorage.setItem('gcovr-expanded-folders', JSON.stringify(paths));
  }

  function renderTree(container, tree) {
    container.innerHTML = '';

    if (!tree || tree.length === 0) {
      container.innerHTML = '<div class="tree-loading">No files found</div>';
      return;
    }

    tree.forEach(function(item) {
      container.appendChild(createTreeItem(item, ''));
    });

    // Auto-expand to current file and highlight it
    expandToCurrentFile(container);
  }

  function expandToCurrentFile(container) {
    // Get current page filename
    var currentPage = window.location.pathname.split('/').pop() || 'index.html';

    // Find the link matching current page
    var currentLink = container.querySelector('a[href="' + currentPage + '"]');

    if (currentLink) {
      // Mark as active
      var treeItem = currentLink.closest('.tree-item');
      if (treeItem) {
        treeItem.classList.add('active');
      }

      // Expand all parent folders
      var parent = currentLink.closest('.tree-children');
      while (parent) {
        var parentItem = parent.closest('.tree-item');
        if (parentItem) {
          parentItem.classList.add('expanded');
          var toggle = parentItem.querySelector(':scope > .tree-item-header > .tree-folder-toggle');
          if (toggle) toggle.textContent = '−';
        }
        parent = parentItem ? parentItem.parentElement.closest('.tree-children') : null;
      }
    }

    // Restore previously expanded folders from localStorage
    try {
      var saved = localStorage.getItem('gcovr-expanded-folders');
      if (saved) {
        var paths = JSON.parse(saved);
        paths.forEach(function(path) {
          var el = container.querySelector('.tree-item[data-tree-path="' + CSS.escape(path) + '"]');
          if (el && !el.classList.contains('no-children')) {
            el.classList.add('expanded');
            var toggle = el.querySelector(':scope > .tree-item-header > .tree-folder-toggle');
            if (toggle) toggle.textContent = '−';
          }
        });
      }
    } catch (e) {
      // Ignore localStorage errors
    }

    // Scroll active item into view instantly
    if (currentLink) {
      currentLink.scrollIntoView({ block: 'center', behavior: 'instant' });
    }
  }

  // Clean relative path prefixes like '../../../' from names
  function cleanPathName(name) {
    if (!name) return 'unknown';
    // Remove leading ./ or ../
    while (name.indexOf('./') === 0 || name.indexOf('../') === 0) {
      if (name.indexOf('./') === 0) {
        name = name.substring(2);
      } else if (name.indexOf('../') === 0) {
        name = name.substring(3);
      }
    }
    return name || 'unknown';
  }

  // Get just the filename from a path
  function getDisplayName(name) {
    var cleaned = cleanPathName(name);
    var lastSlash = cleaned.lastIndexOf('/');
    return lastSlash >= 0 ? cleaned.substring(lastSlash + 1) : cleaned;
  }

  function createTreeItem(item, parentPath) {
    var hasChildren = item.children && item.children.length > 0;
    var isDirectory = item.isDirectory || hasChildren;
    var cleanedName = cleanPathName(item.name);
    var treePath = parentPath ? (parentPath + '/' + cleanedName) : cleanedName;

    var div = document.createElement('div');
    div.className = 'tree-item' + (isDirectory ? ' is-folder' : '') + (hasChildren ? '' : ' no-children');
    div.setAttribute('data-tree-path', treePath);

    var header = document.createElement('div');
    header.className = 'tree-item-header';
    var toggle = null;

    // Toggle button (+/-) for folders with children
    if (hasChildren) {
      toggle = document.createElement('button');
      toggle.className = 'tree-folder-toggle';
      toggle.textContent = '+';
      toggle.setAttribute('aria-label', 'Toggle folder');
      toggle.addEventListener('click', function(e) {
        e.stopPropagation();
        e.preventDefault();
        var isExpanded = div.classList.toggle('expanded');
        toggle.textContent = isExpanded ? '−' : '+';
        saveExpandedFolders();
      });
      header.appendChild(toggle);

      // Make entire header clickable to expand/collapse
      header.style.cursor = 'pointer';
      header.addEventListener('click', function(e) {
        // If clicking directly on a link, let it navigate
        if (e.target.closest('a')) return;
        e.preventDefault();
        var isExpanded = div.classList.toggle('expanded');
        toggle.textContent = isExpanded ? '−' : '+';
        saveExpandedFolders();
      });
    } else {
      var spacer = document.createElement('span');
      spacer.className = 'tree-spacer';
      header.appendChild(spacer);
    }

    // Icon - different for folders vs files
    var icon = document.createElement('span');
    if (isDirectory) {
      icon.className = 'tree-icon tree-icon-folder';
      icon.innerHTML = '<svg viewBox="0 0 16 16" width="16" height="16"><path fill="currentColor" d="M1.75 1A1.75 1.75 0 000 2.75v10.5C0 14.216.784 15 1.75 15h12.5A1.75 1.75 0 0016 13.25v-8.5A1.75 1.75 0 0014.25 3H7.5a.25.25 0 01-.2-.1l-.9-1.2C6.07 1.26 5.55 1 5 1H1.75z"/></svg>';
    } else {
      icon.className = 'tree-icon tree-icon-file';
      icon.innerHTML = '<svg viewBox="0 0 16 16" width="16" height="16"><path fill="currentColor" d="M3.75 1.5a.25.25 0 00-.25.25v12.5c0 .138.112.25.25.25h9.5a.25.25 0 00.25-.25V6h-2.75A1.75 1.75 0 019 4.25V1.5H3.75zm6.75.062V4.25c0 .138.112.25.25.25h2.688l-2.938-2.938zM2 1.75C2 .784 2.784 0 3.75 0h6.586c.464 0 .909.184 1.237.513l2.914 2.914c.329.328.513.773.513 1.237v9.586A1.75 1.75 0 0113.25 16h-9.5A1.75 1.75 0 012 14.25V1.75z"/></svg>';
    }
    header.appendChild(icon);

    // Label (with link if available)
    // Use the full cleaned name so joined-dir entries like "boost/url"
    // display as a single multi-segment label in the sidebar.
    var displayName = cleanPathName(item.name);
    var tooltipText = cleanPathName(item.fullPath || item.name);
    var label = document.createElement('span');
    label.className = 'tree-label';
    label.title = tooltipText;
    if (item.link) {
      var link = document.createElement('a');
      link.href = item.link;
      link.textContent = displayName;
      link.title = tooltipText;
      label.appendChild(link);
    } else {
      label.textContent = displayName;
    }
    header.appendChild(label);

    div.appendChild(header);

    // Children container (for expand/collapse)
    if (hasChildren) {
      var childrenWrapper = document.createElement('div');
      childrenWrapper.className = 'tree-children';

      var childrenInner = document.createElement('div');
      childrenInner.className = 'tree-children-inner';
      item.children.forEach(function(child) {
        childrenInner.appendChild(createTreeItem(child, treePath));
      });

      childrenWrapper.appendChild(childrenInner);
      div.appendChild(childrenWrapper);
    }

    return div;
  }

  // ===========================================
  // Search
  // ===========================================

  function initSearch() {
    const searchInput = document.getElementById('file-search');
    const fileTree = document.getElementById('file-tree');
    const clearBtn = document.getElementById('search-clear');
    const searchContainer = searchInput ? searchInput.closest('.sidebar-search') : null;
    if (!searchInput || !fileTree) return;

    // Store pre-search expanded state so we can restore it
    var preSearchExpanded = null;

    // Create no-results message
    var noResults = document.createElement('div');
    noResults.className = 'search-no-results';
    noResults.textContent = 'No matching files';
    noResults.style.display = 'none';
    fileTree.appendChild(noResults);

    function updateClearButton() {
      if (searchContainer) {
        searchContainer.classList.toggle('has-query', searchInput.value.trim() !== '');
      }
    }

    // Clear button
    if (clearBtn) {
      clearBtn.addEventListener('click', function() {
        searchInput.value = '';
        sessionStorage.removeItem('gcovr-search');
        updateClearButton();
        performSearch('');
        searchInput.focus();
      });
    }

    var debounceTimer = null;
    searchInput.addEventListener('input', function() {
      updateClearButton();
      clearTimeout(debounceTimer);
      debounceTimer = setTimeout(function() {
        var val = searchInput.value;
        if (val.trim() !== '') {
          sessionStorage.setItem('gcovr-search', val);
        } else {
          sessionStorage.removeItem('gcovr-search');
        }
        performSearch(val);
      }, 150);
    });

    // Restore search state from sessionStorage on page load (synchronous
    // since initFileTree has already built the tree before initSearch runs)
    var savedSearch = sessionStorage.getItem('gcovr-search');
    if (savedSearch) {
      searchInput.value = savedSearch;
      updateClearButton();
      performSearch(savedSearch);
    }

    function performSearch(value) {
      var query = value.toLowerCase().trim();
      var allItems = fileTree.querySelectorAll('.tree-item');

      // Clear all highlights
      fileTree.querySelectorAll('.search-highlight').forEach(function(mark) {
        var parent = mark.parentNode;
        parent.replaceChild(document.createTextNode(mark.textContent), mark);
        parent.normalize();
      });

      // If query is empty, restore original state
      if (query === '') {
        noResults.style.display = 'none';
        allItems.forEach(function(item) {
          item.style.display = '';
          item.classList.remove('search-match');
        });
        // Restore pre-search expanded state
        if (preSearchExpanded !== null) {
          allItems.forEach(function(item) {
            var path = item.getAttribute('data-tree-path');
            var toggle = item.querySelector(':scope > .tree-item-header > .tree-folder-toggle');
            if (toggle) {
              if (preSearchExpanded.indexOf(path) >= 0) {
                item.classList.add('expanded');
                toggle.textContent = '\u2212';
              } else {
                item.classList.remove('expanded');
                toggle.textContent = '+';
              }
            }
          });
          preSearchExpanded = null;
        }
        return;
      }

      // Save expanded state before first search
      if (preSearchExpanded === null) {
        preSearchExpanded = [];
        allItems.forEach(function(item) {
          if (item.classList.contains('expanded')) {
            preSearchExpanded.push(item.getAttribute('data-tree-path'));
          }
        });
      }

      // Determine which items match (check full path and display name)
      var matchSet = new Set();

      allItems.forEach(function(item) {
        var path = (item.getAttribute('data-tree-path') || '').toLowerCase();
        var label = item.querySelector(':scope > .tree-item-header > .tree-label');
        var text = label ? label.textContent.toLowerCase() : '';
        if (path.includes(query) || text.includes(query)) {
          matchSet.add(item);
        }
      });

      // Also mark all ancestor items of matches as visible
      var visibleSet = new Set(matchSet);
      matchSet.forEach(function(item) {
        var parent = item.parentElement;
        while (parent && parent !== fileTree) {
          if (parent.classList && parent.classList.contains('tree-item')) {
            visibleSet.add(parent);
          }
          parent = parent.parentElement;
        }
      });

      // Apply visibility, expand parents of matches, highlight text
      var anyVisible = false;
      allItems.forEach(function(item) {
        var isVisible = visibleSet.has(item);
        item.style.display = isVisible ? '' : 'none';
        item.classList.toggle('search-match', matchSet.has(item));

        if (isVisible) {
          anyVisible = true;
          // Auto-expand folders that contain matches
          var toggle = item.querySelector(':scope > .tree-item-header > .tree-folder-toggle');
          if (toggle && visibleSet.has(item) && !matchSet.has(item) || (toggle && matchSet.has(item) && item.classList.contains('is-folder'))) {
            item.classList.add('expanded');
            toggle.textContent = '\u2212';
          }
        }

        // Highlight matched text in label
        if (matchSet.has(item)) {
          var label = item.querySelector(':scope > .tree-item-header > .tree-label');
          if (label) {
            highlightText(label, query);
          }
        }
      });

      noResults.style.display = anyVisible ? 'none' : '';
    }

    function highlightText(container, query) {
      // Walk text nodes inside the label (may be inside an <a> tag)
      var walker = document.createTreeWalker(container, NodeFilter.SHOW_TEXT, null, false);
      var textNodes = [];
      while (walker.nextNode()) {
        textNodes.push(walker.currentNode);
      }
      textNodes.forEach(function(node) {
        var text = node.textContent;
        var lowerText = text.toLowerCase();
        var idx = lowerText.indexOf(query);
        if (idx === -1) return;

        var frag = document.createDocumentFragment();
        var lastIdx = 0;
        while (idx !== -1) {
          if (idx > lastIdx) {
            frag.appendChild(document.createTextNode(text.substring(lastIdx, idx)));
          }
          var mark = document.createElement('mark');
          mark.className = 'search-highlight';
          mark.textContent = text.substring(idx, idx + query.length);
          frag.appendChild(mark);
          lastIdx = idx + query.length;
          idx = lowerText.indexOf(query, lastIdx);
        }
        if (lastIdx < text.length) {
          frag.appendChild(document.createTextNode(text.substring(lastIdx)));
        }
        node.parentNode.replaceChild(frag, node);
      });
    }
  }

  // ===========================================
  // Progressive Function Row Rendering
  // ===========================================

  function initFunctionRows() {
    var dataEl = document.getElementById('functions-data');
    if (!dataEl) return;

    var config = window.__functionsPageConfig || {};
    var data = JSON.parse(dataEl.textContent);
    var container = document.querySelector('.functions-body');
    var loadingEl = document.getElementById('functions-loading');
    var showBranches = config.showBranches;
    var showConditions = config.showConditions;
    var singlePage = config.singlePage;
    var currentFile = config.htmlFilename || '';

    if (data.length === 0) {
      if (loadingEl) loadingEl.remove();
      return;
    }

    // --- Virtual scrolling setup ---
    var ROW_HEIGHT = 52;
    var BUFFER = 10;
    var visibleCount = Math.max(30, Math.ceil(container.clientHeight / ROW_HEIGHT) + BUFFER * 2);
    var viewport, visibleEl;
    var lastStartIdx = -1;

    window.addEventListener('resize', function() {
      visibleCount = Math.max(30, Math.ceil(container.clientHeight / ROW_HEIGHT) + BUFFER * 2);
      lastStartIdx = -1;
      renderVisible();
    });

    function buildHref(entry) {
      if (singlePage) return '#' + entry.html_filename + '|l' + entry.line;
      if (currentFile !== entry.html_filename) return entry.html_filename + '#l' + entry.line;
      return '#l' + entry.line;
    }

    function entryKey(entry) {
      return entry.name + '|' + entry.filename + ':' + entry.line;
    }

    function el(tag, cls, text) {
      var node = document.createElement(tag);
      if (cls) node.className = cls;
      if (text !== undefined) node.textContent = text;
      return node;
    }

    function createRow(entry) {
      var row = el('div', 'function-row');
      if (highlightKey && entryKey(entry) === highlightKey) {
        row.classList.add('function-row-visited');
      }

      // col-function
      var colFn = el('div', 'col-function');
      var a = document.createElement('a');
      a.href = buildHref(entry);
      a.appendChild(el('span', 'function-name', entry.name));
      a.appendChild(el('span', 'function-location', entry.filename + ':' + entry.line));
      colFn.appendChild(a);
      row.appendChild(colFn);

      // col-calls
      var colCalls = el('div', 'col-calls');
      var callSpan;
      if (entry.excluded) {
        callSpan = el('span', 'excluded', 'excluded');
      } else if (entry.execution_count === 0) {
        callSpan = el('span', 'not-called', 'not called');
      } else {
        callSpan = el('span', 'called', entry.execution_count + 'x');
      }
      colCalls.appendChild(callSpan);
      row.appendChild(colCalls);

      // col-lines
      row.appendChild(el('div', 'col-lines', entry.line_coverage + '%'));

      // col-branches (optional)
      if (showBranches) {
        row.appendChild(el('div', 'col-branches', entry.branch_coverage + '%'));
      }

      // col-conditions (optional)
      if (showConditions) {
        row.appendChild(el('div', 'col-conditions', entry.condition_coverage + '%'));
      }

      return row;
    }

    function setupVirtualScroll() {
      if (loadingEl) loadingEl.remove();

      viewport = document.createElement('div');
      viewport.className = 'functions-viewport';
      viewport.style.height = (data.length * ROW_HEIGHT) + 'px';

      visibleEl = document.createElement('div');
      visibleEl.className = 'functions-visible';

      viewport.appendChild(visibleEl);
      container.appendChild(viewport);
    }

    function renderVisible() {
      var scrollTop = container.scrollTop;
      var startIdx = Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - BUFFER);
      var endIdx = Math.min(data.length, startIdx + visibleCount + BUFFER);

      // Skip re-render if the window hasn't shifted
      if (startIdx === lastStartIdx) return;
      lastStartIdx = startIdx;

      visibleEl.style.top = (startIdx * ROW_HEIGHT) + 'px';

      var frag = document.createDocumentFragment();
      for (var i = startIdx; i < endIdx; i++) {
        frag.appendChild(createRow(data[i]));
      }
      visibleEl.replaceChildren(frag);
    }

    // --- Scroll listener (rAF-throttled) ---
    var ticking = false;
    container.addEventListener('scroll', function() {
      if (!ticking) {
        requestAnimationFrame(function() { renderVisible(); ticking = false; });
        ticking = true;
      }
    });

    // --- Save state on navigation for back-button restore ---
    var highlightKey = null;
    container.addEventListener('click', function(e) {
      var row = e.target.closest('.function-row');
      if (!row) return;
      var link = row.querySelector('a');
      if (!link) return;
      var nameEl = row.querySelector('.function-name');
      var locEl = row.querySelector('.function-location');
      if (nameEl && locEl) {
        sessionStorage.setItem('gcovr-functions-clicked', nameEl.textContent + '|' + locEl.textContent);
      }
      sessionStorage.setItem('gcovr-functions-scrollTop', String(container.scrollTop));
    });

    // --- Data-level sorting ---
    function sortData(key, ascending) {
      data.sort(function(a, b) {
        var aVal, bVal;
        switch (key) {
          case 'name': aVal = a.name; bVal = b.name; break;
          case 'calls': aVal = a.excluded ? -1 : a.execution_count; bVal = b.excluded ? -1 : b.execution_count; break;
          case 'lines': aVal = parseFloat(a.line_coverage) || 0; bVal = parseFloat(b.line_coverage) || 0; break;
          case 'branches': aVal = parseFloat(a.branch_coverage) || 0; bVal = parseFloat(b.branch_coverage) || 0; break;
          case 'conditions': aVal = parseFloat(a.condition_coverage) || 0; bVal = parseFloat(b.condition_coverage) || 0; break;
          default: aVal = a.name; bVal = b.name;
        }
        if (typeof aVal === 'string' && typeof bVal === 'string') {
          return ascending ? aVal.localeCompare(bVal) : bVal.localeCompare(aVal);
        }
        return ascending ? aVal - bVal : bVal - aVal;
      });
      lastStartIdx = -1; // force re-render
      viewport.style.height = (data.length * ROW_HEIGHT) + 'px';
      renderVisible();
    }

    // Intercept sort clicks on functions-header before initSorting runs
    var funcHeaders = document.querySelectorAll('.functions-header .sortable');
    funcHeaders.forEach(function(header) {
      header.addEventListener('click', function(e) {
        e.stopPropagation();
        var sortKey = this.dataset.sort;
        var isAscending = this.classList.contains('sorted-ascending');

        // Update header classes
        funcHeaders.forEach(function(h) {
          h.classList.remove('sorted-ascending', 'sorted-descending');
        });
        this.classList.add(isAscending ? 'sorted-descending' : 'sorted-ascending');

        sortData(sortKey, !isAscending);
      }, true); // capture phase to beat initSorting
    });

    // --- Restore saved state (scroll + highlight) ---
    function restoreSavedState() {
      var saved = sessionStorage.getItem('gcovr-functions-clicked');
      if (saved !== null) {
        sessionStorage.removeItem('gcovr-functions-clicked');
        highlightKey = saved;
      }
      var scroll = sessionStorage.getItem('gcovr-functions-scrollTop');
      if (scroll !== null) {
        sessionStorage.removeItem('gcovr-functions-scrollTop');
        container.scrollTop = parseInt(scroll, 10);
      }
      if (saved !== null || scroll !== null) {
        lastStartIdx = -1;
        renderVisible();
      }
    }

    // --- Initialize ---
    data.sort(function(a, b) { return a.name.localeCompare(b.name); });

    setupVirtualScroll();
    renderVisible();
    restoreSavedState();

    // Also restore on bfcache navigation (browser Back button)
    window.addEventListener('pageshow', function(e) {
      if (e.persisted) restoreSavedState();
    });

    // Mark functions page so initSorting can skip it
    container.dataset.virtualScroll = 'true';
  }



  // ===========================================
  // Sorting
  // ===========================================

  function initSorting() {
    var headerSets = [
      {
        selector: '.file-list-header .sortable, .functions-header .sortable',
        getContainer: function() {
          return document.getElementById('file-list') || document.querySelector('.functions-body');
        },
        defaultSort: { key: 'filename', ascending: true }
      },
      {
        selector: '.source-function-header .sortable',
        getContainer: function() {
          return document.querySelector('.source-functions-list');
        },
        defaultSort: null
      }
    ];

    headerSets.forEach(function(set) {
      var headers = document.querySelectorAll(set.selector);
      if (!headers.length) return;

      headers.forEach(function(header) {
        header.addEventListener('click', function() {
          var sortKey = this.dataset.sort;
          var isAscending = this.classList.contains('sorted-ascending');

          headers.forEach(function(h) {
            h.classList.remove('sorted-ascending', 'sorted-descending');
          });

          this.classList.add(isAscending ? 'sorted-descending' : 'sorted-ascending');

          sortList(set.getContainer(), sortKey, !isAscending);
        });
      });

      if (set.defaultSort) {
        sortList(set.getContainer(), set.defaultSort.key, set.defaultSort.ascending);
      }
    });
  }

  function sortList(container, key, ascending) {
    if (!container) return;
    // Virtual scroll handles its own sorting
    if (container.dataset.virtualScroll) return;

    var headerEl = container.querySelector('.source-function-header, .file-list-header, .functions-header');
    var rows = Array.from(container.children).filter(function(el) { return el !== headerEl; });

    rows.sort(function(a, b) {
      // Directories always come first
      var aIsDir = a.classList.contains('directory');
      var bIsDir = b.classList.contains('directory');
      if (aIsDir && !bIsDir) return -1;
      if (!aIsDir && bIsDir) return 1;

      var aVal = a.dataset[key] || a.querySelector('[data-sort]')?.dataset.sort || '';
      var bVal = b.dataset[key] || b.querySelector('[data-sort]')?.dataset.sort || '';
      if (key == 'filename' && localStorage.getItem('gcovr-view-mode') === 'nested') {
        aVal = aVal.split('/').pop();
        bVal = bVal.split('/').pop();
      }

      // Try to parse as numbers
      var aNum = parseFloat(aVal);
      var bNum = parseFloat(bVal);

      if (!isNaN(aNum) && !isNaN(bNum)) {
        return ascending ? aNum - bNum : bNum - aNum;
      }

      // String comparison
      return ascending ? aVal.localeCompare(bVal) : bVal.localeCompare(aVal);
    });

    rows.forEach(function(row) {
      container.appendChild(row);
    });
  }

  // ===========================================
  // Toggle Buttons (Coverage Lines)
  // ===========================================

  function initToggleButtons() {
    const buttons = document.querySelectorAll('.button_toggle_coveredLine, .button_toggle_uncoveredLine, .button_toggle_partialCoveredLine, .button_toggle_excludedLine');

    buttons.forEach(function(button) {
      var lineClass = button.value;
      if (!document.querySelector('.' + lineClass)) {
        button.disabled = true;
        button.classList.remove('show_' + lineClass);
        return;
      }
      button.addEventListener('click', function() {
        const lineClass = this.value;
        const showClass = 'show_' + lineClass;

        // Toggle the button state
        this.classList.toggle(showClass);

        // Toggle visibility of lines
        const lines = document.querySelectorAll('.' + lineClass);
        lines.forEach(function(line) {
          line.classList.toggle(showClass);
        });
        document.dispatchEvent(new CustomEvent('coverage-toggled'));
      });
    });

    // Also handle simpler toggle buttons
    const simpleToggles = document.querySelectorAll('.btn-toggle');
    simpleToggles.forEach(function(button) {
      button.addEventListener('click', function() {
        // Use data attribute to get line class (persists after toggle)
        const lineClass = this.dataset.lineClass;
        if (!lineClass) return;

        const showClass = 'show_' + lineClass;
        this.classList.toggle(showClass);
        const lines = document.querySelectorAll('.' + lineClass);
        lines.forEach(function(line) {
          line.classList.toggle(showClass);
        });
        document.dispatchEvent(new CustomEvent('coverage-toggled'));
      });
    });
  }

  // ===========================================
  // Coverage Navigation (prev/next uncovered)
  // ===========================================

  function initCoverageNav() {
    var prevBtn = document.getElementById('nav-prev');
    var nextBtn = document.getElementById('nav-next');
    var counter = document.getElementById('nav-counter');

    if (!prevBtn || !nextBtn || !counter) return;

    var gapLines = [];
    var currentIndex = -1;

    function collectGapLines() {
      var uncovered = document.querySelectorAll('tr.uncoveredLine.show_uncoveredLine');
      var partial = document.querySelectorAll('tr.partialCoveredLine.show_partialCoveredLine');
      var merged = [];
      var i;
      for (i = 0; i < uncovered.length; i++) merged.push(uncovered[i]);
      for (i = 0; i < partial.length; i++) merged.push(partial[i]);
      // Sort by DOM order
      merged.sort(function(a, b) {
        var pos = a.compareDocumentPosition(b);
        if (pos & Node.DOCUMENT_POSITION_FOLLOWING) return -1;
        if (pos & Node.DOCUMENT_POSITION_PRECEDING) return 1;
        return 0;
      });
      gapLines = merged;
      currentIndex = -1;
      updateCounter();
    }

    function updateCounter() {
      if (gapLines.length === 0) {
        counter.textContent = 'All lines covered';
        prevBtn.disabled = true;
        nextBtn.disabled = true;
      } else {
        var display = currentIndex >= 0 ? (currentIndex + 1) : 0;
        counter.textContent = display + ' / ' + gapLines.length;
        prevBtn.disabled = false;
        nextBtn.disabled = false;
      }
    }

    function navigateTo(index) {
      if (gapLines.length === 0) return;
      // Remove previous highlight
      var prev = document.querySelector('tr.source-line.nav-highlight');
      if (prev) prev.classList.remove('nav-highlight');

      currentIndex = index;
      var row = gapLines[currentIndex];
      row.scrollIntoView({ block: 'center', behavior: 'instant' });
      row.classList.add('nav-highlight');
      setTimeout(function() {
        row.classList.remove('nav-highlight');
      }, 1500);
      updateCounter();
    }

    function nextGap() {
      if (gapLines.length === 0) return;
      var next = currentIndex + 1;
      if (next >= gapLines.length) next = 0;
      navigateTo(next);
    }

    function prevGap() {
      if (gapLines.length === 0) return;
      var prev = currentIndex - 1;
      if (prev < 0) prev = gapLines.length - 1;
      navigateTo(prev);
    }

    prevBtn.addEventListener('click', prevGap);
    nextBtn.addEventListener('click', nextGap);

    document.addEventListener('keydown', function(e) {
      var tag = (e.target.tagName || '').toLowerCase();
      if (tag === 'input' || tag === 'textarea' || e.target.isContentEditable) return;
      if (e.key === 'n') nextGap();
      if (e.key === 'p') prevGap();
    });

    document.addEventListener('coverage-toggled', function() {
      collectGapLines();
    });

    collectGapLines();
  }

  // ===========================================
  // View Toggle (Nested / Flat)
  // ===========================================

  function initViewToggle() {
    var toggleContainer = document.getElementById('view-toggle');
    var fileList = document.getElementById('file-list');
    var appContainer = document.querySelector('.app-container');

    if (!toggleContainer) return;

    // Always show the toggle
    toggleContainer.style.display = '';

    var buttons = toggleContainer.querySelectorAll('.view-btn');
    var savedView = localStorage.getItem('gcovr-view-mode');

    function setActiveButton(view) {
      buttons.forEach(function(btn) {
        btn.classList.toggle('active', btn.dataset.view === view);
      });
    }

    // On non-directory pages (file/source views), still respect flat mode for sidebar
    if (!fileList) {
      if (appContainer && savedView === 'flat') {
        appContainer.classList.add('flat-mode');
        setActiveButton('flat');
      }

      // Allow toggling view mode from source pages
      buttons.forEach(function(btn) {
        btn.addEventListener('click', function() {
          var view = this.dataset.view;
          localStorage.setItem('gcovr-view-mode', view);
          setActiveButton(view);
          if (appContainer) {
            if (view === 'flat') {
              appContainer.classList.add('flat-mode');
            } else {
              appContainer.classList.remove('flat-mode');
              document.documentElement.classList.remove('early-flat-mode');
            }
          }
        });
      });
      return;
    }

    var originalNodes = null; // stash for restoring nested view

    function collectFlatFiles(nodes, parentPath) {
      var results = [];
      for (var i = 0; i < nodes.length; i++) {
        var node = nodes[i];
        var cleanedName = node.name;
        // Remove leading ./ or ../
        while (cleanedName.indexOf('./') === 0 || cleanedName.indexOf('../') === 0) {
          cleanedName = cleanedName.indexOf('./') === 0 ? cleanedName.substring(2) : cleanedName.substring(3);
        }
        var fullPath = parentPath ? (parentPath + '/' + cleanedName) : cleanedName;

        if (node.isDirectory && node.children && node.children.length > 0) {
          results = results.concat(collectFlatFiles(node.children, fullPath));
        } else if (!node.isDirectory) {
          var copy = {};
          for (var key in node) {
            if (node.hasOwnProperty(key)) copy[key] = node[key];
          }
          copy.fullPath = fullPath;
          results.push(copy);
        }
      }
      return results;
    }

    function buildFlatRow(file) {
      var row = document.createElement('div');
      row.className = 'file-row file';
      row.setAttribute('data-filename', file.fullPath);
      row.setAttribute('data-coverage', file.coverage || '0');
      row.setAttribute('data-lines', file.linesTotal || '');
      row.setAttribute('data-functions', file.functionsCoverage || '');
      row.setAttribute('data-branches', file.branchesCoverage || '');

      // Col name
      var colName = document.createElement('div');
      colName.className = 'col-name';

      var icon = document.createElement('span');
      icon.className = 'file-icon';
      icon.innerHTML = '<svg viewBox="0 0 16 16" width="16" height="16"><path fill="currentColor" d="M3.75 1.5a.25.25 0 00-.25.25v12.5c0 .138.112.25.25.25h9.5a.25.25 0 00.25-.25V6h-2.75A1.75 1.75 0 019 4.25V1.5H3.75zm6.75.062V4.25c0 .138.112.25.25.25h2.688a.252.252 0 00-.011-.013l-2.914-2.914a.272.272 0 00-.013-.011zM2 1.75C2 .784 2.784 0 3.75 0h6.586c.464 0 .909.184 1.237.513l2.914 2.914c.329.328.513.773.513 1.237v9.586A1.75 1.75 0 0113.25 16h-9.5A1.75 1.75 0 012 14.25V1.75z"></path></svg>';
      colName.appendChild(icon);

      if (file.link) {
        var a = document.createElement('a');
        a.href = file.link;
        a.textContent = file.fullPath;
        a.title = file.fullPath;
        colName.appendChild(a);
      } else {
        var span = document.createElement('span');
        span.className = 'no-link';
        span.textContent = file.fullPath;
        span.title = file.fullPath;
        colName.appendChild(span);
      }
      row.appendChild(colName);

      // Col coverage
      var colCov = document.createElement('div');
      colCov.className = 'col-coverage';

      var barContainer = document.createElement('div');
      barContainer.className = 'coverage-bar-container';
      var bar = document.createElement('div');
      var linesCov = file.linesCoverage || '';
      var linesClass = file.linesClass || file.coverageClass || '';
      bar.className = 'coverage-bar ' + linesClass;
      bar.style.width = (linesCov && linesCov !== '-') ? linesCov + '%' : '0%';
      barContainer.appendChild(bar);
      colCov.appendChild(barContainer);

      var pct = document.createElement('span');
      pct.className = 'coverage-percent ' + linesClass;
      pct.textContent = (linesCov && linesCov !== '-') ? linesCov + '%' : '-';
      colCov.appendChild(pct);
      row.appendChild(colCov);

      // Col lines
      var colLines = document.createElement('div');
      colLines.className = 'col-lines';
      var execSpan = document.createElement('span');
      execSpan.className = 'stat-value';
      execSpan.textContent = file.linesExec || '';
      colLines.appendChild(execSpan);
      var sep = document.createElement('span');
      sep.className = 'stat-separator';
      sep.textContent = '/';
      colLines.appendChild(sep);
      var totalSpan = document.createElement('span');
      totalSpan.className = 'stat-total';
      totalSpan.textContent = file.linesTotal || '';
      colLines.appendChild(totalSpan);
      row.appendChild(colLines);

      // Col functions (check if container has the column)
      var container = fileList.closest('.file-list-container');
      var hasFunctions = !container || !container.classList.contains('no-functions');
      var hasBranches = !container || !container.classList.contains('no-branches');
      var hasConditions = !container || !container.classList.contains('no-conditions');
      var hasDecision = !container || !container.classList.contains('no-decisions');
      var hasCalls = !container || !container.classList.contains('no-calls');

      if (hasFunctions) {
        var colFunc = document.createElement('div');
        colFunc.className = 'col-functions';
        var funcVal = document.createElement('span');
        var funcCov = file.functionsCoverage || '';
        var funcClass = file.functionsClass || '';
        funcVal.className = 'stat-value ' + funcClass;
        funcVal.textContent = (funcCov && funcCov !== '-') ? funcCov + '%' : '-';
        colFunc.appendChild(funcVal);
        row.appendChild(colFunc);
      }

      if (hasBranches) {
        var colBr = document.createElement('div');
        colBr.className = 'col-branches';
        var brVal = document.createElement('span');
        var brCov = file.branchesCoverage || '';
        var brClass = file.branchesClass || '';
        brVal.className = 'stat-value ' + brClass;
        brVal.textContent = (brCov && brCov !== '-') ? brCov + '%' : '-';
        colBr.appendChild(brVal);
        row.appendChild(colBr);
      }

      if (hasConditions) {
        var colCond = document.createElement('div');
        colCond.className = 'col-conditions';
        var condVal = document.createElement('span');
        var condCov = file.conditionsCoverage || '';
        var condClass = file.conditionsClass || '';
        condVal.className = 'stat-value ' + condClass;
        condVal.textContent = (condCov && condCov !== '-') ? condCov + '%' : '-';
        colCond.appendChild(condVal);
        row.appendChild(colCond);
      }

      if (hasDecision) {
        var colDec = document.createElement('div');
        colDec.className = 'col-decision';
        var decVal = document.createElement('span');
        var decCov = file.decisionCoverage || '';
        var decClass = file.decisionClass || '';
        decVal.className = 'stat-value ' + decClass;
        decVal.textContent = (decCov && decCov !== '-') ? decCov + '%' : '-';
        colDec.appendChild(decVal);
        row.appendChild(colDec);
      }

      if (hasCalls) {
        var colCalls = document.createElement('div');
        colCalls.className = 'col-calls';
        var callsVal = document.createElement('span');
        var callsCov = file.callsCoverage || '';
        var callsClass = file.callsClass || '';
        callsVal.className = 'stat-value ' + callsClass;
        callsVal.textContent = (callsCov && callsCov !== '-') ? callsCov + '%' : '-';
        colCalls.appendChild(callsVal);
        row.appendChild(colCalls);
      }


      return row;
    }

    function switchToFlat() {
      if (!window.GCOVR_TREE_DATA) return;

      // Stash original DOM nodes
      if (originalNodes === null) {
        originalNodes = document.createDocumentFragment();
        while (fileList.firstChild) {
          originalNodes.appendChild(fileList.firstChild);
        }
      }

      var flatFiles = collectFlatFiles(window.GCOVR_TREE_DATA, '');

      // Sort by coverage ascending (matching default)
      flatFiles.sort(function(a, b) {
        var aVal = parseFloat(a.coverage) || 0;
        var bVal = parseFloat(b.coverage) || 0;
        return aVal - bVal;
      });

      while (fileList.firstChild) {
        fileList.removeChild(fileList.firstChild);
      }
      for (var i = 0; i < flatFiles.length; i++) {
        fileList.appendChild(buildFlatRow(flatFiles[i]));
      }

      if (appContainer) appContainer.classList.add('flat-mode');
      setActiveButton('flat');
      localStorage.setItem('gcovr-view-mode', 'flat');
    }

    function switchToNested() {
      if (originalNodes !== null) {
        while (fileList.firstChild) {
          fileList.removeChild(fileList.firstChild);
        }
        fileList.appendChild(originalNodes);
        originalNodes = null;
      }
      if (appContainer) appContainer.classList.remove('flat-mode');
      document.documentElement.classList.remove('early-flat-mode');
      setActiveButton('nested');
      localStorage.setItem('gcovr-view-mode', 'nested');

      // Re-run sorting to maintain state
      sortList(document.getElementById('file-list') || document.querySelector('.functions-body'), 'filename', true);
    }

    buttons.forEach(function(btn) {
      btn.addEventListener('click', function() {
        var view = this.dataset.view;
        if (view === 'flat') {
          switchToFlat();
        } else {
          switchToNested();
        }
      });
    });

    // Apply saved preference on load
    if (savedView === 'flat') {
      // Defer to ensure tree data is loaded
      setTimeout(function() {
        switchToFlat();
      }, 0);
    }
  }

  // ===========================================
  // Settings Dropdown (mobile gear icon)
  // ===========================================

  function initSettingsDropdown() {
    var btn = document.getElementById('settings-btn');
    var dropdown = document.getElementById('settings-dropdown');
    var header = document.querySelector('.main-header');
    if (!btn || !dropdown || !header) return;

    var viewToggle = document.getElementById('view-toggle');
    var themeToggle = document.getElementById('theme-toggle');
    var isMobile = false;

    // Reference node: settings-btn, so we can insert before it when moving back
    function moveToDropdown() {
      if (viewToggle && viewToggle.parentNode !== dropdown) {
        dropdown.appendChild(viewToggle);
      }
      if (themeToggle && themeToggle.parentNode !== dropdown) {
        dropdown.appendChild(themeToggle);
      }
    }

    function moveToHeader() {
      // Insert before settings-btn so they appear in original order
      if (viewToggle && viewToggle.parentNode !== header) {
        header.insertBefore(viewToggle, btn);
      }
      if (themeToggle && themeToggle.parentNode !== header) {
        header.insertBefore(themeToggle, btn);
      }
    }

    function checkBreakpoint() {
      var nowMobile = window.innerWidth <= 1024;
      if (nowMobile === isMobile) return;
      isMobile = nowMobile;
      if (isMobile) {
        moveToDropdown();
      } else {
        dropdown.classList.remove('open');
        moveToHeader();
      }
    }

    // Toggle dropdown on button click
    btn.addEventListener('click', function(e) {
      e.stopPropagation();
      dropdown.classList.toggle('open');
    });

    // Close on outside click
    document.addEventListener('click', function(e) {
      if (!dropdown.contains(e.target) && e.target !== btn) {
        dropdown.classList.remove('open');
      }
    });

    // Close on Escape
    document.addEventListener('keydown', function(e) {
      if (e.key === 'Escape') {
        dropdown.classList.remove('open');
      }
    });

    // Respond to resize
    window.addEventListener('resize', checkBreakpoint);

    // Initial check
    checkBreakpoint();
  }

  // ===========================================
  // Popup Resize (only when overflowing)
  // ===========================================

  function initPopupResize() {
    var details = document.querySelectorAll('.branch-details, .condition-details, .decision-details, .call-details');
    if (details.length === 0) return;

    details.forEach(function(det) {
      det.addEventListener('toggle', function() {
        if (!det.open) return;
        var popup = det.querySelector('.branch-popup, .condition-popup, .decision-popup, .call-popup');
        if (!popup) return;
        // Check after render if content overflows
        requestAnimationFrame(function() {
          if (popup.scrollHeight > popup.clientHeight) {
            popup.classList.add('is-overflowing');
          } else {
            popup.classList.remove('is-overflowing');
          }
        });
      });
    });
  }

  // ===========================================
  // Nav Override (prev/next follows tree order)
  // ===========================================

  function initNavOverride() {
    if (!window.GCOVR_TREE_DATA) return;

    var navPrev = document.querySelectorAll('.nav-prev');
    var navNext = document.querySelectorAll('.nav-next');
    if (navPrev.length === 0 && navNext.length === 0) return;

    // DFS-flatten tree to collect file links in sidebar order
    function collectLinks(nodes) {
      var links = [];
      for (var i = 0; i < nodes.length; i++) {
        var node = nodes[i];
        if (node.isDirectory && node.children && node.children.length > 0) {
          links = links.concat(collectLinks(node.children));
        } else if (!node.isDirectory && node.link) {
          links.push(node.link);
        }
      }
      return links;
    }

    var fileLinks = collectLinks(window.GCOVR_TREE_DATA);
    if (fileLinks.length === 0) return;

    var currentPage = window.location.pathname.split('/').pop() || 'index.html';
    var idx = fileLinks.indexOf(currentPage);
    if (idx === -1) return;

    var prev = idx > 0 ? fileLinks[idx - 1] : null;
    var next = idx < fileLinks.length - 1 ? fileLinks[idx + 1] : null;

    function updateNavLinks(els, href) {
      for (var i = 0; i < els.length; i++) {
        var el = els[i];
        if (href) {
          // Enable: ensure it's an <a> with the correct href
          if (el.tagName === 'A') {
            el.setAttribute('href', href);
          } else {
            // Replace disabled <span> with an <a>
            var a = document.createElement('a');
            a.className = el.className.replace(/\bdisabled\b/, '').trim();
            a.href = href;
            a.title = el.title;
            while (el.firstChild) a.appendChild(el.firstChild);
            el.parentNode.replaceChild(a, el);
          }
        } else {
          // Disable: ensure it's a <span> with disabled class
          if (el.tagName === 'A') {
            var span = document.createElement('span');
            span.className = el.className + ' disabled';
            span.title = el.title;
            while (el.firstChild) span.appendChild(el.firstChild);
            el.parentNode.replaceChild(span, el);
          } else {
            el.classList.add('disabled');
          }
        }
      }
    }

    updateNavLinks(navPrev, prev);
    updateNavLinks(navNext, next);
  }

  // ===========================================
  // TLA Navigation (HIT/MIS/PAR links)
  // ===========================================

  function initTlaNavigation() {
    var rows = document.querySelectorAll('.source-line');
    if (rows.length === 0) return;

    // Classify each row by coverage type
    var COV_CLASSES = ['coveredLine', 'uncoveredLine', 'partialCoveredLine'];
    var LABELS = { coveredLine: 'HIT', uncoveredLine: 'MIS', partialCoveredLine: 'PAR' };
    var CSS_CLASSES = { coveredLine: 'tla-hit', uncoveredLine: 'tla-mis', partialCoveredLine: 'tla-par' };

    // Build list of groups: contiguous runs of the same coverage class
    var groups = []; // { type, firstRow }
    var prevType = null;

    for (var i = 0; i < rows.length; i++) {
      var row = rows[i];
      var type = null;
      for (var j = 0; j < COV_CLASSES.length; j++) {
        if (row.classList.contains(COV_CLASSES[j])) {
          type = COV_CLASSES[j];
          break;
        }
      }
      if (type === null) {
        prevType = null;
        continue;
      }
      if (type !== prevType) {
        groups.push({ type: type, firstRow: row });
        prevType = type;
      }
    }

    if (groups.length === 0) return;

    // Determine the anchor prefix used in this page
    var sampleAnchor = rows[0].querySelector('.col-lineno a');
    var anchorPrefix = '';
    if (sampleAnchor) {
      var id = sampleAnchor.id;
      var idx = id.indexOf('l');
      if (idx > 0) {
        anchorPrefix = id.substring(0, idx);
      }
    }

    // For each group, find the line number from its first row
    function getLineNo(row) {
      var a = row.querySelector('.col-lineno a');
      return a ? a.textContent.trim() : '';
    }

    // Build per-type lists for wrap-around linking
    var byType = {};
    for (var i = 0; i < groups.length; i++) {
      var g = groups[i];
      if (!byType[g.type]) byType[g.type] = [];
      byType[g.type].push(i);
    }

    // For each group, compute next group index of same type
    var nextGroupIdx = new Array(groups.length);
    for (var type in byType) {
      var indices = byType[type];
      for (var k = 0; k < indices.length; k++) {
        var nextK = (k + 1) % indices.length;
        nextGroupIdx[indices[k]] = indices[nextK];
      }
    }

    // Inject TLA links
    for (var i = 0; i < groups.length; i++) {
      var g = groups[i];
      var cell = g.firstRow.querySelector('.col-tla');
      if (!cell) continue;

      var targetGroup = groups[nextGroupIdx[i]];
      var targetLineNo = getLineNo(targetGroup.firstRow);

      var targetId = anchorPrefix + 'l' + targetLineNo;

      var a = document.createElement('a');
      a.className = 'tla-link ' + CSS_CLASSES[g.type];
      a.textContent = LABELS[g.type];
      a.href = '#' + targetId;
      a.addEventListener('click', function(e) {
        var target = document.getElementById(this.getAttribute('href').substring(1));
        if (target) {
          e.preventDefault();
          // Scroll within the source-table-container
          var scrollBox = document.querySelector('.source-table-container');
          var row = target.closest('tr');
          if (scrollBox && row) {
            var thead = scrollBox.querySelector('thead');
            var theadHeight = thead ? thead.offsetHeight : 0;
            scrollBox.scrollTo({ top: row.offsetTop - theadHeight - 8, behavior: 'instant' });
          }
          history.replaceState(null, '', this.getAttribute('href'));
          // Highlight the target row (clear any previous highlight first)
          var prev = document.querySelector('.highlight-target');
          if (prev) prev.classList.remove('highlight-target');
          if (row) row.classList.add('highlight-target');
        }
      });
      cell.appendChild(a);
    }
  }

  // ===========================================
  // Line number click highlight
  // ===========================================

  function initLineHighlight() {
    var clickedFnItem = null;

    function highlightFromHash(scroll) {
      var prev = document.querySelector('.highlight-target');
      if (prev) prev.classList.remove('highlight-target');
      var prevFn = document.querySelector('.source-function-item.selected');
      if (prevFn) prevFn.classList.remove('selected');
      var id = window.location.hash.slice(1);
      if (!id) return;
      var el = document.getElementById(id);
      if (!el) return;
      var fnItem = clickedFnItem || document.querySelector('.source-function-item[href="#' + id + '"]');
      clickedFnItem = null;
      if (fnItem) fnItem.classList.add('selected');
      var row = el.closest('tr');
      if (row) {
        row.classList.add('highlight-target');
        if (scroll) {
          var scrollBox = document.querySelector('.source-table-container');
          if (scrollBox) {
            var thead = scrollBox.querySelector('thead');
            var theadHeight = thead ? thead.offsetHeight : 0;
            scrollBox.scrollTo({ top: row.offsetTop - theadHeight - 8, behavior: 'instant' });
          } else {
            row.scrollIntoView({ block: 'center' });
          }
        }
      }
    }

    // Handle clicks on function list items directly
    var fnList = document.querySelector('.source-functions-list');
    if (fnList) {
      fnList.addEventListener('click', function(e) {
        var item = e.target.closest('.source-function-item');
        if (!item) return;
        e.preventDefault();
        clickedFnItem = item;
        var href = item.getAttribute('href');
        if (href) history.replaceState(null, '', href);
        highlightFromHash(true);
      });
    }

    // Event delegation: single listener on the table container
    var container = document.querySelector('.source-table-container');
    if (container) {
      container.addEventListener('click', function(e) {
        var anchor = e.target.closest('.col-lineno a');
        if (!anchor) return;
        e.preventDefault();
        if (anchor.id) history.replaceState(null, '', '#' + anchor.id);
        highlightFromHash(false);
      });
    }

    // Highlight + scroll on initial load and back/forward navigation
    highlightFromHash(true);
    window.addEventListener('hashchange', function() { highlightFromHash(true); });
  }

  // ===========================================
  // Column Visibility Toggles
  // ===========================================

  function initColumnToggles() {
    var buttons = document.querySelectorAll('.col-toggle');
    if (buttons.length === 0) return;

    var table = document.querySelector('.source-table');
    if (!table) return;

    // Restore saved state
    var hidden = [];
    try {
      var saved = localStorage.getItem('gcovr-hidden-columns');
      if (saved) {
        hidden = JSON.parse(saved);
      } else {
        hidden = ['tla'];
      }
    } catch (e) {}

    // Apply saved hidden columns
    var fnList = document.querySelector('.source-functions-list');
    for (var i = 0; i < hidden.length; i++) {
      table.classList.add('hide-col-' + hidden[i]);
      if (fnList) {
        fnList.classList.add('hide-col-' + hidden[i]);
      }
    }

    // Update button appearance to match state
    buttons.forEach(function(btn) {
      var col = btn.getAttribute('data-col');
      if (hidden.indexOf(col) >= 0) {
        btn.classList.remove('show-col');
      }
    });

    // Handle clicks
    buttons.forEach(function(btn) {
      btn.addEventListener('click', function() {
        var col = this.getAttribute('data-col');
        var hideClass = 'hide-col-' + col;
        var isHidden = table.classList.toggle(hideClass);
        this.classList.toggle('show-col', !isHidden);

        // Sync with function list sidebar
        var fnList = document.querySelector('.source-functions-list');
        if (fnList) {
          fnList.classList.toggle(hideClass, isHidden);
        }

        // Save state
        var current = [];
        var allBtns = document.querySelectorAll('.col-toggle');
        allBtns.forEach(function(b) {
          if (!b.classList.contains('show-col')) {
            current.push(b.getAttribute('data-col'));
          }
        });
        localStorage.setItem('gcovr-hidden-columns', JSON.stringify(current));
      });
    });
  }

  // ===========================================
  // File nav keyboard shortcuts ([ and ])
  // ===========================================

  function initFileNavKeys() {
    var prevLink = document.querySelector('.source-nav-links .nav-prev') || document.querySelector('.nav-links .nav-prev');
    var nextLink = document.querySelector('.source-nav-links .nav-next') || document.querySelector('.nav-links .nav-next');
    if (!prevLink && !nextLink) return;

    document.addEventListener('keydown', function(e) {
      var tag = (e.target.tagName || '').toLowerCase();
      if (tag === 'input' || tag === 'textarea' || e.target.isContentEditable) return;
      // Re-query to pick up any DOM replacements by initNavOverride
      var prev = document.querySelector('.source-nav-links a.nav-prev') || document.querySelector('.nav-links a.nav-prev');
      var next = document.querySelector('.source-nav-links a.nav-next') || document.querySelector('.nav-links a.nav-next');
      if (e.key === '[' && prev) {
        window.location.href = prev.href;
      }
      if (e.key === ']' && next) {
        window.location.href = next.href;
      }
    });
  }

  // ===========================================
  // Enrich file nav tooltips with actual filenames
  // ===========================================

  function initFileNavTooltips() {
    if (!window.GCOVR_TREE_DATA) return;
    var links = document.querySelectorAll('.source-nav-links .nav-prev, .source-nav-links .nav-next, .nav-links .nav-prev, .nav-links .nav-next');
    for (var i = 0; i < links.length; i++) {
      var anchor = links[i];
      var href = anchor.getAttribute('href');
      if (!href || href === '#') continue;
      var filename = href.replace(/^.*\//, '').replace(/#.*$/, '');
      var node = findNodeInTree(window.GCOVR_TREE_DATA, filename);
      if (node && node.name) {
        var direction = anchor.classList.contains('nav-prev') ? 'Previous' : 'Next';
        anchor.title = direction + ': ' + node.name;
      }
    }
  }

  function findNodeInTree(nodes, targetLink) {
    for (var i = 0; i < nodes.length; i++) {
      var node = nodes[i];
      if (node.link === targetLink) return node;
      if (node.children) {
        var found = findNodeInTree(node.children, targetLink);
        if (found) return found;
      }
    }
    return null;
  }

  // ===========================================
  // Prefetch pages on hover for instant nav
  // ===========================================

  function initPrefetch() {
    // Skip for file:// protocol (fetch won't work)
    if (location.protocol === 'file:') return;

    var prefetched = {};

    document.addEventListener('mouseover', function(e) {
      var link = e.target.closest('a[href]');
      if (!link) return;

      var href = link.getAttribute('href');
      // Only prefetch local HTML pages
      if (!href || href.charAt(0) === '#' || href.indexOf('://') !== -1) return;
      if (prefetched[href]) return;

      prefetched[href] = true;
      var prefetchLink = document.createElement('link');
      prefetchLink.rel = 'prefetch';
      prefetchLink.href = href;
      document.head.appendChild(prefetchLink);
    });
  }

  function initFunctionListPersistence() {
    var details = document.querySelector('details.source-functions');
    if (!details) return;

    var key = 'gcovr-fn-list-open';
    if (sessionStorage.getItem(key) === 'true') {
      details.setAttribute('open', '');
    }

    details.addEventListener('toggle', function() {
      sessionStorage.setItem(key, details.open ? 'true' : 'false');
    });
  }

})();

window.GCOVR_TREE_DATA = [
  {
    "branchesClass": "coverage-unknown",
    "branchesCoverage": "-",
    "children": [
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "children": [
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "36",
            "linesTotal": "36",
            "link": "index.buffer_copy.hpp.34e3716bbe44e989ab4a6f8c42af4440.html",
            "name": "buffer_copy.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "97.5",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "31",
            "linesTotal": "31",
            "link": "index.buffer_param.hpp.dd4c0f98fe69407f84e6801b22bd4665.html",
            "name": "buffer_param.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "2",
            "linesTotal": "2",
            "link": "index.buffer_slice.hpp.7148317e2956bfb9ac8ed3e716ddb700.html",
            "name": "buffer_slice.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "17",
            "linesTotal": "17",
            "link": "index.circular_dynamic_buffer.hpp.537907c0315811263b495cc2b3f466dd.html",
            "name": "circular_dynamic_buffer.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "35",
            "linesTotal": "35",
            "link": "index.flat_dynamic_buffer.hpp.42996dadaa9e62741a25b851f1ddbf53.html",
            "name": "flat_dynamic_buffer.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "47",
            "linesTotal": "47",
            "link": "index.make_buffer.hpp.0b5191be327d1df5306d419e4fbd7a4f.html",
            "name": "make_buffer.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "53",
            "linesTotal": "53",
            "link": "index.string_dynamic_buffer.hpp.763d1cd31ae554f6eac21385dca732b7.html",
            "name": "string_dynamic_buffer.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "51",
            "linesTotal": "51",
            "link": "index.vector_dynamic_buffer.hpp.b0343a8f9ccfd3c519274dc554e47f63.html",
            "name": "vector_dynamic_buffer.hpp"
          }
        ],
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "99.3",
        "isDirectory": true,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "272",
        "linesTotal": "272",
        "link": "index.buffers.38f127d0edd594a2706520d325a9e771.html",
        "name": "buffers"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "children": [
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-none",
            "functionsCoverage": "0.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "4",
            "linesTotal": "4",
            "link": "index.decomposes_to.hpp.4e035c93864630e817ede6fd4b5b2517.html",
            "name": "decomposes_to.hpp"
          }
        ],
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-none",
        "functionsCoverage": "0.0",
        "isDirectory": true,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "4",
        "linesTotal": "4",
        "link": "index.concept.d2f8dd85c319399c60f33d24642000e7.html",
        "name": "concept"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "children": [
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "9",
            "linesTotal": "9",
            "link": "index.await_suspend_helper.hpp.8b7ff23e2ccd515acf4a752ce8d73603.html",
            "name": "await_suspend_helper.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "87",
            "linesTotal": "87",
            "link": "index.buffer_array.hpp.4c9bafb147366274c006dcbd0192f943.html",
            "name": "buffer_array.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-low",
            "functionsCoverage": "71.4",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "11",
            "linesTotal": "11",
            "link": "index.frame_memory_resource.hpp.0f2def30855cadd7c9014f2db396bacd.html",
            "name": "frame_memory_resource.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "81",
            "linesTotal": "81",
            "link": "index.intrusive.hpp.c2369106b5448bd1df29f7990777fa73.html",
            "name": "intrusive.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "93.3",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "15",
            "linesTotal": "15",
            "link": "index.io_result_combinators.hpp.1ce79c5ee14103d41d7b049e2fe2824f.html",
            "name": "io_result_combinators.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "4",
            "linesTotal": "4",
            "link": "index.run.hpp.80826a4038222b772691e3d1e7a0e2b9.html",
            "name": "run.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-low",
            "functionsCoverage": "51.1",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "29",
            "linesTotal": "29",
            "link": "index.run_callbacks.hpp.30199c30457c68d35a46bb322f251caf.html",
            "name": "run_callbacks.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "96.3",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "112",
            "linesTotal": "113",
            "link": "index.slice_impl.hpp.3b6a4f8939a854320407978dcf41e767.html",
            "name": "slice_impl.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "12",
            "linesTotal": "12",
            "link": "index.thread_local_ptr.hpp.1d18d329ea5fe4a9e15cb2b5e959af69.html",
            "name": "thread_local_ptr.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "2",
            "linesTotal": "2",
            "link": "index.type_id.hpp.e496de65857f7dcba917ee9964b2fb42.html",
            "name": "type_id.hpp"
          }
        ],
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-low",
        "functionsCoverage": "66.6",
        "isDirectory": true,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "362",
        "linesTotal": "363",
        "link": "index.detail.2427ad8b285090028a707090faec2f5d.html",
        "name": "detail"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "children": [
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "children": [
              {
                "branchesClass": "coverage-unknown",
                "branchesCoverage": "-",
                "coverage": "100.0",
                "coverageClass": "coverage-high",
                "functionsClass": "coverage-high",
                "functionsCoverage": "100.0",
                "isDirectory": false,
                "linesClass": "coverage-high",
                "linesCoverage": "100.0",
                "linesExec": "5",
                "linesTotal": "5",
                "link": "index.timer_service.hpp.240cb07ffb90c1f565048358df407be7.html",
                "name": "timer_service.hpp"
              }
            ],
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": true,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "5",
            "linesTotal": "5",
            "link": "index.detail.2b3e6e890856957b2c3b73af45694fdc.html",
            "name": "detail"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-medium",
            "functionsCoverage": "81.8",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "53",
            "linesTotal": "53",
            "link": "index.any_executor.hpp.4d4d2368eecdf0d1aca9ace9dc0360f0.html",
            "name": "any_executor.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "66",
            "linesTotal": "66",
            "link": "index.async_event.hpp.29453227df0cac28b84f2d9372f12e75.html",
            "name": "async_event.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "91",
            "linesTotal": "91",
            "link": "index.async_mutex.hpp.7e55e22c6eeeb7ce305e8887243efb4b.html",
            "name": "async_mutex.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "52",
            "linesTotal": "52",
            "link": "index.execution_context.hpp.eff3a48db9644ffdc1377de0ca7a8be0.html",
            "name": "execution_context.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-low",
            "functionsCoverage": "40.6",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "46",
            "linesTotal": "46",
            "link": "index.executor_ref.hpp.f39a9bdcd6839b078c2bc4dea57c20da.html",
            "name": "executor_ref.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "19",
            "linesTotal": "19",
            "link": "index.frame_alloc_mixin.hpp.36f503592fa8fc01d85f87b01cd9dd76.html",
            "name": "frame_alloc_mixin.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "12",
            "linesTotal": "12",
            "link": "index.frame_allocator.hpp.fc27441cb76bf9d6bacc04bfa3e557f6.html",
            "name": "frame_allocator.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-medium",
            "functionsCoverage": "84.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "22",
            "linesTotal": "22",
            "link": "index.immediate.hpp.fe94226635754ba10b5c09bd7041396e.html",
            "name": "immediate.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "35",
            "linesTotal": "38",
            "link": "index.io_awaitable_promise_base.hpp.7cd520b7ab8bf46438c68934a802a51d.html",
            "name": "io_awaitable_promise_base.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "94.1",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "94.1",
            "linesExec": "48",
            "linesTotal": "51",
            "link": "index.recycling_memory_resource.hpp.55c9fffd026133071320d62223eecd35.html",
            "name": "recycling_memory_resource.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "189",
            "linesTotal": "190",
            "link": "index.run.hpp.c52bb307158f9b104255d8e364986d4c.html",
            "name": "run.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "98.5",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "152",
            "linesTotal": "154",
            "link": "index.run_async.hpp.ab296551cbf8717971100f39f0826a97.html",
            "name": "run_async.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-medium",
            "functionsCoverage": "86.5",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "28",
            "linesTotal": "28",
            "link": "index.strand.hpp.a47df7a8d160f5274aded9311019536a.html",
            "name": "strand.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "7",
            "linesTotal": "7",
            "link": "index.thread_pool.hpp.8e9a8fdede6a75d756c9f8225848c78f.html",
            "name": "thread_pool.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "31",
            "linesTotal": "31",
            "link": "index.work_guard.hpp.0f8bd2583144748024d0667ea72ae45e.html",
            "name": "work_guard.hpp"
          }
        ],
        "coverage": "99.7",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "97.1",
        "isDirectory": true,
        "linesClass": "coverage-high",
        "linesCoverage": "99.7",
        "linesExec": "856",
        "linesTotal": "865",
        "link": "index.ex.e698ef32095988fba8036e7b68c06e57.html",
        "name": "ex"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "children": [
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-medium",
            "functionsCoverage": "86.3",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "220",
            "linesTotal": "220",
            "link": "index.any_buffer_sink.hpp.385c03aaebc97771e1fb671943037253.html",
            "name": "any_buffer_sink.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-medium",
            "functionsCoverage": "82.6",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "138",
            "linesTotal": "138",
            "link": "index.any_buffer_source.hpp.8700c9eb013f712349e5e41961bd7346.html",
            "name": "any_buffer_source.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-medium",
            "functionsCoverage": "78.1",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "84",
            "linesTotal": "84",
            "link": "index.any_read_source.hpp.a0c3c4bef1d1ef40a7bf2b5d7288cbcc.html",
            "name": "any_read_source.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-low",
            "functionsCoverage": "72.3",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "68",
            "linesTotal": "88",
            "link": "index.any_read_stream.hpp.c1874ab4ea88b65e6f22c65ae1bfac25.html",
            "name": "any_read_stream.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "46",
            "linesTotal": "49",
            "link": "index.any_stream.hpp.27453f689d0af034a945b47a5e1e2fd0.html",
            "name": "any_stream.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-medium",
            "functionsCoverage": "83.9",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "169",
            "linesTotal": "169",
            "link": "index.any_write_sink.hpp.bd9fbb6434796c1ad86ae6391f2d73d4.html",
            "name": "any_write_sink.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-low",
            "functionsCoverage": "73.2",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "94",
            "linesTotal": "94",
            "link": "index.any_write_stream.hpp.4ff939454650c77b58b1e023d9132049.html",
            "name": "any_write_stream.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "4",
            "linesTotal": "4",
            "link": "index.pull_from.hpp.3a0a1e7f6d1cadded13e182f0e9b00f6.html",
            "name": "pull_from.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "4",
            "linesTotal": "4",
            "link": "index.push_to.hpp.3c0e80e46605024fb40082f53f9929c9.html",
            "name": "push_to.hpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "97.7",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "57",
            "linesTotal": "57",
            "link": "index.write_now.hpp.d4d4286e69caa0c9af16a34a2932e3b6.html",
            "name": "write_now.hpp"
          }
        ],
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-medium",
        "functionsCoverage": "83.4",
        "isDirectory": true,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "884",
        "linesTotal": "907",
        "link": "index.io.8dd6dc18bc986ac5e08f7c465f508b51.html",
        "name": "io"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "95.9",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "65",
        "linesTotal": "65",
        "link": "index.buffers.hpp.519279fcf62d09d3a03b359051b03049.html",
        "name": "buffers.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "3",
        "linesTotal": "3",
        "link": "index.cond.hpp.9721a0a2fa9e7762a7ddc39ddb6de9ed.html",
        "name": "cond.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "54",
        "linesTotal": "54",
        "link": "index.delay.hpp.ddff7f25d2290557b2cece979117d585.html",
        "name": "delay.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "3",
        "linesTotal": "3",
        "link": "index.error.hpp.1d165fc5fd869abebacca69baa425b9b.html",
        "name": "error.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "8",
        "linesTotal": "8",
        "link": "index.io_result.hpp.82d8e9f3faa8cac796fec88d73afefb0.html",
        "name": "io_result.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "97.2",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "81",
        "linesTotal": "81",
        "link": "index.quitter.hpp.ec604665a95e5b520b35f5e9a736c8c4.html",
        "name": "quitter.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "6",
        "linesTotal": "6",
        "link": "index.read.hpp.1a2705195a5c9e77e26def5db36457c6.html",
        "name": "read.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "90.5",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "65",
        "linesTotal": "66",
        "link": "index.read_until.hpp.e5f555aa07a4e2cd6ec7d7416cec73bd.html",
        "name": "read_until.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "92.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "76",
        "linesTotal": "77",
        "link": "index.task.hpp.c5223cf33aaf82e234b701a6aa444ddc.html",
        "name": "task.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "46",
        "linesTotal": "46",
        "link": "index.timeout.hpp.61be4b5308f95e1f1239969cce57e8d9.html",
        "name": "timeout.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "95.6",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "161",
        "linesTotal": "162",
        "link": "index.when_all.hpp.590c7248e848f72c035876f2c83741aa.html",
        "name": "when_all.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "94.7",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "165",
        "linesTotal": "166",
        "link": "index.when_any.hpp.c0e4177c3dfb23edea7f4103d4478841.html",
        "name": "when_any.hpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "2",
        "linesTotal": "2",
        "link": "index.write.hpp.9f1360bb97ada4d6fa104ab9eb9f735a.html",
        "name": "write.hpp"
      }
    ],
    "coverage": "99.9",
    "coverageClass": "coverage-high",
    "functionsClass": "coverage-high",
    "functionsCoverage": "92.3",
    "isDirectory": true,
    "linesClass": "coverage-high",
    "linesCoverage": "99.9",
    "linesExec": "3113",
    "linesTotal": "3150",
    "link": "index.capy.5b3034c9edc02983e01f0da525c8eb54.html",
    "name": "include/boost/capy"
  },
  {
    "branchesClass": "coverage-unknown",
    "branchesCoverage": "-",
    "children": [
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "children": [
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "33",
            "linesTotal": "33",
            "link": "index.circular_dynamic_buffer.cpp.56cfc4793085b51ee5fd594c544cc13f.html",
            "name": "circular_dynamic_buffer.cpp"
          }
        ],
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": true,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "33",
        "linesTotal": "33",
        "link": "index.buffers.4f79c9e5ce456b11fd9080cc7175b203.html",
        "name": "buffers"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "children": [
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "20",
            "linesTotal": "20",
            "link": "index.except.cpp.4645be379eebfa472ccd344a47a6674d.html",
            "name": "except.cpp"
          }
        ],
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": true,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "20",
        "linesTotal": "20",
        "link": "index.detail.8efc291289a587be34f5679ec1c70004.html",
        "name": "detail"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "children": [
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "children": [
              {
                "branchesClass": "coverage-unknown",
                "branchesCoverage": "-",
                "coverage": "100.0",
                "coverageClass": "coverage-high",
                "functionsClass": "coverage-high",
                "functionsCoverage": "100.0",
                "isDirectory": false,
                "linesClass": "coverage-high",
                "linesCoverage": "100.0",
                "linesExec": "21",
                "linesTotal": "21",
                "link": "index.strand_queue.hpp.c781f88a7d24ec177721c7d79450e226.html",
                "name": "strand_queue.hpp"
              },
              {
                "branchesClass": "coverage-unknown",
                "branchesCoverage": "-",
                "coverage": "100.0",
                "coverageClass": "coverage-high",
                "functionsClass": "coverage-high",
                "functionsCoverage": "100.0",
                "isDirectory": false,
                "linesClass": "coverage-high",
                "linesCoverage": "100.0",
                "linesExec": "113",
                "linesTotal": "114",
                "link": "index.strand_service.cpp.bf429b9a050f274aa122b488f6ce3407.html",
                "name": "strand_service.cpp"
              },
              {
                "branchesClass": "coverage-unknown",
                "branchesCoverage": "-",
                "coverage": "100.0",
                "coverageClass": "coverage-high",
                "functionsClass": "coverage-high",
                "functionsCoverage": "100.0",
                "isDirectory": false,
                "linesClass": "coverage-high",
                "linesCoverage": "100.0",
                "linesExec": "62",
                "linesTotal": "62",
                "link": "index.timer_service.cpp.bc9c39af4d9fa3858ad641999346e111.html",
                "name": "timer_service.cpp"
              }
            ],
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": true,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "196",
            "linesTotal": "197",
            "link": "index.detail.c17ca3bd931bf88752cb60e2b40b7c11.html",
            "name": "detail"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "66",
            "linesTotal": "70",
            "link": "index.execution_context.cpp.9850ddb4ba842c507c64a28e64b9bbd6.html",
            "name": "execution_context.cpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "26",
            "linesTotal": "26",
            "link": "index.recycling_memory_resource.cpp.8ff974fc1a4fa4705f2be85827171c0c.html",
            "name": "recycling_memory_resource.cpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "3",
            "linesTotal": "3",
            "link": "index.system_context.cpp.99ec24b3001d71d684a726221f158a08.html",
            "name": "system_context.cpp"
          },
          {
            "branchesClass": "coverage-unknown",
            "branchesCoverage": "-",
            "coverage": "100.0",
            "coverageClass": "coverage-high",
            "functionsClass": "coverage-high",
            "functionsCoverage": "100.0",
            "isDirectory": false,
            "linesClass": "coverage-high",
            "linesCoverage": "100.0",
            "linesExec": "139",
            "linesTotal": "139",
            "link": "index.thread_pool.cpp.c775b997894b725dc2823fc39c684d1c.html",
            "name": "thread_pool.cpp"
          }
        ],
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": true,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "430",
        "linesTotal": "435",
        "link": "index.ex.2b6503f49da1d89e22bf32803574820d.html",
        "name": "ex"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "29",
        "linesTotal": "29",
        "link": "index.cond.cpp.5cd054a222a124c8353604a80d404c16.html",
        "name": "cond.cpp"
      },
      {
        "branchesClass": "coverage-unknown",
        "branchesCoverage": "-",
        "coverage": "100.0",
        "coverageClass": "coverage-high",
        "functionsClass": "coverage-high",
        "functionsCoverage": "100.0",
        "isDirectory": false,
        "linesClass": "coverage-high",
        "linesCoverage": "100.0",
        "linesExec": "20",
        "linesTotal": "20",
        "link": "index.error.cpp.d3b44edfe5b0e6a13719399208fd02ce.html",
        "name": "error.cpp"
      }
    ],
    "coverage": "100.0",
    "coverageClass": "coverage-high",
    "functionsClass": "coverage-high",
    "functionsCoverage": "100.0",
    "isDirectory": true,
    "linesClass": "coverage-high",
    "linesCoverage": "100.0",
    "linesExec": "532",
    "linesTotal": "537",
    "link": "index.src.25d902c24283ab8cfbac54dfa101ad31.html",
    "name": "src"
  }
];
