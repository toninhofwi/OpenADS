/**
 * ACE editor mode and theme for ADS SQL.
 *
 * Mode:  ace/mode/ads_sql   — extends ace/mode/sql with ADS-specific keywords,
 *                              functions, data types, and // + /* * / comments.
 * Theme: ace/theme/heidisql — HeidiSQL-inspired light syntax highlighting.
 */

// ── Custom theme: HeidiSQL-like light colours ──────────────────────────────
ace.define('ace/theme/heidisql', ['require', 'exports', 'module', 'ace/lib/dom'],
function(require, exports, module) {
    exports.isDark    = false;
    exports.cssClass  = 'ace-heidisql';
    exports.cssText   = [
        '.ace-heidisql                      { background:#fff; color:#000; }',
        '.ace-heidisql .ace_gutter          { background:#f0f0f0; color:#777; }',
        '.ace-heidisql .ace_gutter-active-line{ background:#dce6ff; }',
        '.ace-heidisql .ace_active-line     { background:#eef3ff; }',
        '.ace-heidisql .ace_selection       { background:#b8d4fb; }',
        '.ace-heidisql .ace_cursor          { color:#000; border-left:2px solid #000; }',
        /* keywords: SELECT FROM WHERE JOIN … */
        '.ace-heidisql .ace_keyword         { color:#0033b3; font-weight:bold; }',
        /* storage types: INT VARCHAR DATE … */
        '.ace-heidisql .ace_storage         { color:#0033b3; font-weight:bold; }',
        '.ace-heidisql .ace_storage\\.ace_type{ color:#0033b3; font-weight:bold; }',
        /* built-in functions: COUNT MAX TIMESTAMPADD … */
        '.ace-heidisql .ace_support         { color:#7b00b7; }',
        '.ace-heidisql .ace_support\\.ace_function { color:#7b00b7; }',
        /* string literals */
        '.ace-heidisql .ace_string          { color:#c0392b; }',
        /* comments */
        '.ace-heidisql .ace_comment         { color:#267f00; font-style:italic; }',
        /* numbers */
        '.ace-heidisql .ace_constant\\.ace_numeric { color:#1068c2; }',
        /* boolean constants TRUE FALSE NULL */
        '.ace-heidisql .ace_constant\\.ace_language{ color:#0033b3; font-weight:bold; }',
        /* operators = < > + - … */
        '.ace-heidisql .ace_keyword\\.ace_operator { color:#000; }',
        /* parentheses */
        '.ace-heidisql .ace_paren           { color:#000; }',
    ].join('\n');
    const dom = require('ace/lib/dom');
    dom.importCssString(exports.cssText, exports.cssClass, false);
});

// ── Custom mode: ADS SQL ───────────────────────────────────────────────────
ace.define('ace/mode/ads_sql',
    ['require', 'exports', 'module',
     'ace/lib/oop',
     'ace/mode/sql',
     'ace/mode/sql_highlight_rules',
     'ace/mode/text_highlight_rules'],
function(require, exports, module) {
    'use strict';
    const oop  = require('ace/lib/oop');
    const SqlMode = require('ace/mode/sql').Mode;
    const SqlHighlightRules = require('ace/mode/sql_highlight_rules').SqlHighlightRules;

    // ── Highlight rules ──────────────────────────────────────────────────────
    const AdsHighlightRules = function() {
        SqlHighlightRules.call(this);

        // ADS SQL DML / DDL / control keywords (case-insensitive via mapper below)
        const kw =
            'select|insert|update|delete|from|where|and|or|not|in|is|null|like|between|' +
            'exists|any|all|some|except|intersect|union|as|distinct|top|with|' +
            'inner|left|right|full|outer|join|on|using|cross|natural|' +
            'group|by|order|having|limit|offset|rows|only|percent|ties|' +
            'case|when|then|else|end|if|elsif|begin|end|commit|rollback|transaction|' +
            'savepoint|declare|cursor|open|fetch|next|close|deallocate|' +
            'return|returns|execute|exec|call|procedure|proc|function|trigger|' +
            'before|after|instead|of|for|each|row|statement|' +
            'merge|matched|' +
            'create|alter|drop|table|view|index|database|schema|column|constraint|' +
            'primary|foreign|key|references|unique|check|default|add|modify|rename|to|' +
            'grant|revoke|' +
            'over|partition|window|range|unbounded|preceding|following|current|row|' +
            'output|inserted|deleted|into|values|set|' +
            'asc|desc|nulls|first|last|' +
            'start|while|loop|exit|continue|' +
            'cast|convert|new|old|error';

        // ADS SQL built-in functions — shown in a distinct colour
        const fn =
            /* aggregate */
            'count|sum|avg|min|max|first|last|stddev|variance|' +
            /* string */
            'upper|lower|ucase|lcase|ltrim|rtrim|trim|left|right|mid|' +
            'substr|substring|length|len|locate|instr|strindex|' +
            'replace|concat|space|replicate|reverse|ascii|char|unicode|nchar|' +
            'stuff|charindex|patindex|' +
            /* date/time */
            'now|curdate|curtime|year|month|day|hour|minute|second|' +
            'dayofweek|dayofyear|quarter|dayname|monthname|week|weekday|' +
            'timestampadd|timestampdiff|dateadd|datediff|datepart|extract|' +
            'date|time|timestamp|dateformat|' +
            /* math */
            'abs|ceiling|floor|round|sqrt|power|mod|' +
            'sin|cos|tan|asin|acos|atan|atan2|log|log10|exp|pi|rand|sign|' +
            /* null / conditional */
            'coalesce|nullif|isnull|ifnull|nvl|nvl2|iif|decode|' +
            /* ranking / window */
            'rank|dense_rank|row_number|lag|lead|ntile|percent_rank|cume_dist|' +
            'over|' +
            /* ADS-specific */
            'newseqkey|getvalue|format|greatest|least|' +
            /* type conversion */
            'to_char|to_date|to_number|to_timestamp';

        // SQL/ADS data types → storage.type token
        const type =
            'int|integer|bigint|smallint|tinyint|float|double|real|' +
            'decimal|numeric|money|smallmoney|' +
            'char|varchar|nvarchar|text|ntext|memo|clob|' +
            'bit|boolean|logical|binary|varbinary|blob|image|' +
            'date|time|datetime|timestamp|rowversion|autoinc';

        // Boolean / language constants
        const lang = 'true|false|null';

        const mapper = this.createKeywordMapper({
            'support.function'  : fn,
            'keyword'           : kw,
            'storage.type'      : type,
            'constant.language' : lang,
        }, 'identifier', /* case-insensitive = */ true);

        this.$rules = {
            start: [
                // Line comments: -- and //
                { token: 'comment',         regex: '--.*$' },
                { token: 'comment',         regex: '//.*$' },
                // Block comments /* … */
                { token: 'comment',         regex: '\\/\\*', next: 'block_comment' },
                // Double-quoted string (identifier or string depending on dialect)
                { token: 'string',          regex: '"',    next: 'dq_string' },
                // Single-quoted string
                { token: 'string',          regex: "'",    next: 'sq_string' },
                // Back-tick string
                { token: 'string',          regex: '`',    next: 'bt_string' },
                // Numbers
                { token: 'constant.numeric', regex: '[+-]?\\d+(?:\\.\\d*)?(?:[eE][+-]?\\d+)?\\b' },
                // Keywords / functions / types / identifiers
                { token: mapper,            regex: '[a-zA-Z_$][a-zA-Z0-9_$]*\\b' },
                // Operators
                { token: 'keyword.operator', regex: '[+\\-*/%<>=!&|^~]+' },
                // Parentheses
                { token: 'paren.lparen',    regex: '[(]' },
                { token: 'paren.rparen',    regex: '[)]' },
                // Whitespace
                { token: 'text',            regex: '\\s+' },
            ],
            block_comment: [
                { token: 'comment', regex: '\\*\\/', next: 'start' },
                { defaultToken: 'comment' },
            ],
            dq_string: [
                { token: 'string', regex: '"',  next: 'start' },
                { token: 'string', regex: '""', merge: true },
                { defaultToken: 'string' },
            ],
            sq_string: [
                { token: 'string', regex: "'",  next: 'start' },
                { token: 'string', regex: "''", merge: true },
                { defaultToken: 'string' },
            ],
            bt_string: [
                { token: 'string', regex: '`',  next: 'start' },
                { defaultToken: 'string' },
            ],
        };

        this.normalizeRules();
    };
    oop.inherits(AdsHighlightRules, SqlHighlightRules);

    // ── Mode ────────────────────────────────────────────────────────────────
    const Mode = function() {
        SqlMode.call(this);
        this.HighlightRules = AdsHighlightRules;
        this.lineCommentStart  = ['--', '//'];
        this.blockComment      = { start: '/*', end: '*/' };
    };
    oop.inherits(Mode, SqlMode);

    Mode.prototype.$id = 'ace/mode/ads_sql';
    exports.Mode = Mode;
});
