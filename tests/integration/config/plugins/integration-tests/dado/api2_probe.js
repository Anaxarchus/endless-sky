// dados/runtime.js
var MAX_SAFE = 9007199254740991;
var MAX_SAFE_N = 9007199254740991n;
function canon(r) {
  return r >= -MAX_SAFE_N && r <= MAX_SAFE_N ? Number(r) : r;
}
function wide(a, b, op) {
  const x = BigInt(a), y = BigInt(b);
  switch (op) {
    case 0:
      return canon(BigInt.asIntN(64, x + y));
    case 1:
      return canon(BigInt.asIntN(64, x - y));
    case 2:
      return canon(BigInt.asIntN(64, x * y));
    case 3:
      return canon(BigInt.asIntN(64, x / y));
    case 4:
      return canon(BigInt.asIntN(64, x % y));
    case 5:
      return canon(BigInt.asIntN(64, x & y));
    case 6:
      return canon(BigInt.asIntN(64, x | y));
    case 7:
      return canon(BigInt.asIntN(64, x ^ y));
    default:
      return canon(BigInt.asIntN(64, x & ~y));
  }
}
function __dados_int_add(a, b) {
  if (typeof a === "number" && typeof b === "number") {
    const r = a + b;
    if (r >= -MAX_SAFE && r <= MAX_SAFE) return r + 0;
  }
  return wide(a, b, 0);
}
function zero_divisor(b) {
  if (b === 0 || b === 0n) throw __dados_fault("integer division by zero");
}
function __dados_int_mod(a, b) {
  zero_divisor(b);
  if (typeof a === "number" && typeof b === "number") return a % b + 0;
  return wide(a, b, 4);
}
function __dados_int_of_float(x) {
  if (x !== x) return 0;
  if (x >= 9223372036854776e3) return 9223372036854775807n;
  if (x < -9223372036854776e3) return -9223372036854775808n;
  const t = Math.trunc(x);
  if (t >= -MAX_SAFE && t <= MAX_SAFE) return t + 0;
  return BigInt(t);
}
var DEFAULT_CODE = 70;
var ERROR_CODE = -2147483648;
function code_of(code) {
  if (code === 0) return ERROR_CODE;
  return typeof code === "number" && (code | 0) === code ? code : DEFAULT_CODE;
}
var __dados_Location = class {
  constructor(file, line, column, fn) {
    this.file = file;
    this.line = line;
    this.column = column;
    this.function = fn;
    Object.freeze(this);
  }
};
var __dados_nowhere = new __dados_Location("", 0, 0, "");
function __dados_str_location(l) {
  return l.file === "" ? "<unknown>" : l.file + ":" + l.line + ":" + l.column;
}
function at_line(l) {
  return "  at " + __dados_str_location(l) + (l.function === "" ? "" : " (" + l.function + ")");
}
var modules = /* @__PURE__ */ new Map();
function stack_text(above) {
  const holder = {};
  if (typeof Error.captureStackTrace === "function") {
    Error.captureStackTrace(holder, above);
    return holder.stack;
  }
  return new Error().stack;
}
var FRAME = /^\s*at (?:.* \()?(.+?):(\d+):(\d+)\)?$/;
function frames_of(text) {
  const out = [];
  if (typeof text !== "string") return out;
  for (const line of text.split("\n")) {
    const m = FRAME.exec(line);
    if (m !== null) out.push([m[1], Number(m[2])]);
  }
  return out;
}
function __dados_lines(file, fns, table, files) {
  const here = frames_of(stack_text(__dados_lines));
  if (here.length > 0) modules.set(here[0][0], { file, fns, table, files });
}
function position(frame) {
  const m = modules.get(frame[0]);
  if (m === void 0) return null;
  const t = m.table;
  let lo = 0, hi = t.length / 4 - 1, found = -1;
  while (lo <= hi) {
    const mid = lo + hi >> 1;
    if (t[mid * 4] <= frame[1]) {
      found = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  const fn = t[found * 4 + 3];
  if (found < 0 || fn < 0) return null;
  const file = m.files === void 0 ? m.file : m.files[fn];
  return new __dados_Location(file, t[found * 4 + 1], t[found * 4 + 2], m.fns[fn]);
}
var __dados_Error = class {
  constructor(message, code, location) {
    this.message = message;
    this.code = code_of(code);
    this.__at = location;
    this.__raw = null;
    this.__frames = null;
  }
  __mapped() {
    if (this.__frames === null) {
      const out = [];
      for (const f of frames_of(this.__raw)) {
        const p = position(f);
        if (p !== null) out.push(p);
      }
      this.__frames = out;
    }
    return this.__frames;
  }
  get location() {
    if (this.__at === null) {
      const f = this.__mapped();
      this.__at = f.length > 0 ? f[0] : __dados_nowhere;
    }
    return this.__at;
  }
  get trace() {
    const f = this.__mapped();
    const at = this.location;
    const first = f.length > 0 && f[0].file === at.file && f[0].line === at.line;
    return f.slice(first ? 1 : 0);
  }
  toString() {
    return "error " + this.code + ": " + this.message;
  }
  // The report's frame lines: the location, then the trace.
  get stack() {
    let out = at_line(this.location) + "\n";
    for (const l of this.trace) out += at_line(l) + "\n";
    return out;
  }
};
function __dados_fault(message) {
  const e = new __dados_Error(message, DEFAULT_CODE, null);
  e.__raw = stack_text(__dados_fault);
  return e;
}
function __dados_failure(thrown) {
  if (thrown instanceof __dados_Error) return thrown;
  let message;
  let code = DEFAULT_CODE;
  let raw = null;
  if (thrown instanceof Error) {
    message = String(thrown.message);
    if (typeof thrown.code === "number") code = thrown.code;
    if (typeof thrown.stack === "string") raw = thrown.stack;
  } else {
    try {
      message = typeof thrown === "string" ? thrown : String(thrown);
    } catch (_) {
      message = "a thrown value that has no text";
    }
  }
  const e = new __dados_Error(message, code, null);
  e.__raw = raw;
  return e;
}
Object.defineProperty(globalThis, "__dados_failure", { value: __dados_failure, configurable: true });

// es/Mod.js
var Mod$;
function Mod$class() {
  if (Mod$ === void 0) {
    class Mod2 {
      constructor(...args) {
        this.$fields();
        this.init(...args);
      }
      init() {
      }
      $fields() {
      }
      api_version() {
        return 7;
      }
      name() {
        return "unnamed mod";
      }
      on_load(game) {
        return;
      }
      on_step(game, step) {
        return;
      }
      on_day(game, day) {
        return;
      }
      on_enter(game, system) {
        return;
      }
      on_planet_button(game) {
        return;
      }
      on_panel(game, button) {
        return;
      }
      can_outfitter(game, outfit) {
        return true;
      }
      on_outfitter(game, outfit) {
        return;
      }
      on_confirm(game, token) {
        return;
      }
      on_panel_row(game, row) {
        return;
      }
      on_panel_header(game, column) {
        return;
      }
      on_number(game, token, value) {
        return;
      }
      log(game, text) {
        game.log(this.name() + ": " + text);
      }
      every(step, seconds) {
        let period = __dados_int_of_float(seconds * 60);
        if (period < 1) {
          return true;
        }
        return __dados_int_mod(step, period) === 0;
      }
      player_ships(game) {
        let out = [];
        {
          const end$1 = game.ship_count();
          for (let i = 0; i < end$1; i = __dados_int_add(i, 1)) {
            if (game.is_player(i) && !game.is_disabled(i)) {
              out.push(i);
            }
          }
        }
        return out;
      }
    }
    Mod$ = Mod2;
  }
  return Mod$;
}
var Mod = Mod$class();
__dados_lines("es/Mod.dados", ["es.Mod.api_version", "es.Mod.name", "es.Mod.on_load", "es.Mod.on_step", "es.Mod.on_day", "es.Mod.on_enter", "es.Mod.on_planet_button", "es.Mod.on_panel", "es.Mod.can_outfitter", "es.Mod.on_outfitter", "es.Mod.on_confirm", "es.Mod.on_panel_row", "es.Mod.on_panel_header", "es.Mod.on_number", "es.Mod.log", "es.Mod.every", "es.Mod.player_ships"], [
  5,
  0,
  0,
  -1,
  20,
  25,
  5,
  0,
  21,
  0,
  0,
  -1,
  24,
  29,
  5,
  1,
  25,
  0,
  0,
  -1,
  28,
  33,
  5,
  2,
  29,
  0,
  0,
  -1,
  32,
  37,
  5,
  3,
  33,
  0,
  0,
  -1,
  36,
  42,
  5,
  4,
  37,
  0,
  0,
  -1,
  40,
  47,
  5,
  5,
  41,
  0,
  0,
  -1,
  44,
  52,
  5,
  6,
  45,
  0,
  0,
  -1,
  48,
  56,
  5,
  7,
  49,
  0,
  0,
  -1,
  52,
  62,
  5,
  8,
  53,
  0,
  0,
  -1,
  56,
  67,
  5,
  9,
  57,
  0,
  0,
  -1,
  60,
  72,
  5,
  10,
  61,
  0,
  0,
  -1,
  64,
  77,
  5,
  11,
  65,
  0,
  0,
  -1,
  68,
  82,
  5,
  12,
  69,
  0,
  0,
  -1,
  72,
  86,
  5,
  13,
  73,
  0,
  0,
  -1,
  76,
  90,
  5,
  14,
  77,
  0,
  0,
  -1,
  80,
  94,
  5,
  15,
  81,
  95,
  5,
  15,
  82,
  96,
  9,
  15,
  83,
  95,
  5,
  15,
  84,
  97,
  5,
  15,
  85,
  0,
  0,
  -1,
  88,
  101,
  5,
  16,
  89,
  102,
  5,
  16,
  92,
  103,
  9,
  16,
  93,
  104,
  13,
  16,
  94,
  103,
  9,
  16,
  95,
  102,
  5,
  16,
  97,
  105,
  5,
  16,
  98,
  0,
  0,
  -1
]);

// main/Api2Probe.js
var Api2Probe = class extends Mod$class() {
  $fields() {
    super.$fields();
    this.panel_clicks = 0;
  }
  name() {
    return "api2 probe";
  }
  enabled(game) {
    return game.condition("test: api2") !== 0;
  }
  rec(game, key, value) {
    game.set_condition("test: api2 " + key, value);
  }
  flag(game, key, ok) {
    this.rec(game, key, ok ? 1 : 0);
  }
  describe(game, system) {
    let out = system + ":" + game.system_government(system) + ":";
    out = out + (game.inhabited(system) ? "1" : "0") + ":";
    {
      const end$1 = game.link_count(system);
      for (let k = 0; k < end$1; k = __dados_int_add(k, 1)) {
        if (k > 0) {
          out = out + ",";
        }
        out = out + game.link(system, k);
      }
    }
    out = out + ":";
    {
      const end$2 = game.defense_count(system);
      for (let k = 0; k < end$2; k = __dados_int_add(k, 1)) {
        if (k > 0) {
          out = out + ",";
        }
        out = out + game.defense_fleet(system, k);
      }
    }
    return out;
  }
  on_step(game, step) {
    if (!this.enabled(game)) {
      return;
    }
    let phase = game.condition("test: api2 phase");
    if (phase === 0 || phase === game.condition("test: api2 phase done")) {
      return;
    }
    this.rec(game, "phase done", phase);
    if (phase === 1) {
      this.galaxy(game);
      this.governments(game);
      this.changes(game);
      this.spawning(game);
      this.overlays(game);
    } else if (phase === 2) {
      this.inspect(game);
    } else if (phase === 3) {
      this.kills(game);
    }
  }
  galaxy(game) {
    this.rec(game, "day", game.day());
    this.rec(game, "here " + game.here(), 1);
    this.rec(game, "display " + game.system(), 1);
    let n = game.system_count();
    this.rec(game, "system count", n);
    let all = "";
    {
      const end$3 = n;
      for (let i = 0; i < end$3; i = __dados_int_add(i, 1)) {
        all = all + this.describe(game, game.system_at(i)) + ";";
      }
    }
    this.rec(game, "galaxy " + all, 1);
    this.flag(game, "out of range", game.system_at(-1) === "" && game.system_at(n) === "" && game.link("Alpha", 99) === "" && game.link("Alpha", -1) === "" && game.defense_fleet("Alpha", 5) === "" && game.system_government("Nowhere") === "" && game.link_count("Nowhere") === 0 && !game.inhabited("Nowhere"));
  }
  governments(game) {
    this.rec(game, "attitude pirate republic", __dados_int_of_float(game.attitude("Pirate", "Republic") * 100));
    this.rec(game, "reputation republic", __dados_int_of_float(game.reputation("Republic") * 100));
    game.set_reputation("Pirate", 12.5);
    this.rec(game, "reputation pirate", __dados_int_of_float(game.reputation("Pirate") * 100));
    this.rec(game, "strength raid", __dados_int_of_float(game.fleet_strength("Pirate Raid")));
    this.rec(game, "strength none", __dados_int_of_float(game.fleet_strength("No Such Fleet")));
    for (let stat of ["ships", "hull", "shields", "dps", "speed", "cost", "bogus"]) {
      this.rec(game, "stat raid " + stat, __dados_int_of_float(game.fleet_stat("Pirate Raid", stat)));
    }
    this.rec(game, "stat none", __dados_int_of_float(game.fleet_stat("No Such Fleet", "hull")));
    for (let system of ["Delta", "Alpha"]) {
      let list = "";
      {
        const end$6 = game.system_fleet_count(system);
        for (let k = 0; k < end$6; k = __dados_int_add(k, 1)) {
          list = list + game.system_fleet(system, k) + ",";
        }
      }
      this.rec(game, "system fleets " + system + ": " + list, 1);
    }
    this.flag(game, "system fleet out of range", game.system_fleet("Delta", 2) === "" && game.system_fleet("Delta", -1) === "" && game.system_fleet_count("Nowhere") === 0);
  }
  changes(game) {
    this.flag(game, "change zeta", game.change('system "Zeta"\n	government "Republic"'));
    this.rec(game, "zeta gov " + game.system_government("Zeta"), 1);
    this.flag(game, "change nowhere", game.change('system "Nowhere"\n	government "Republic"'));
  }
  spawning(game) {
    this.rec(game, "spawn raid", game.spawn("Pirate Raid", "Pirate", "api2 raid", false));
    this.rec(game, "spawn trader", game.spawn("Merchant", "Merchant", "api2 trader", true));
    this.rec(game, "spawn none", game.spawn("No Such Fleet", "Pirate", "api2 none", false));
    this.rec(game, "spawn ships now", game.ship_count());
    this.rec(game, "tag strength spawned", __dados_int_of_float(game.tag_strength("api2 raid")));
  }
  overlays(game) {
    game.map_line("Alpha", "Gamma", 0, 0, 1);
    game.map_detail("Gamma", "Old", "dropped", 1, 1, 1);
    game.map_clear();
    game.map_detail("Delta", "Raiders", "3 ships\nbound for Alpha", 0.5, 0, 0);
    game.map_detail("Nowhere", "Lost", "never shown", 1, 1, 1);
    game.map_line("Alpha", "Beta", 1, 0, 0);
    game.map_line("Beta", "Delta", 1, 0.5, 0);
    game.map_marker("Delta", 0, 1, 0, "pirates");
    game.planet_button("Raid board");
    game.panel_open("Not now", "on_step is not the player's action");
    game.panel_button("Ignored");
  }
  inspect(game) {
    let raid = 0;
    let pirate = 0;
    let trader = 0;
    let strength = 0;
    {
      const end$7 = game.ship_count();
      for (let i = 0; i < end$7; i = __dados_int_add(i, 1)) {
        if (game.ship_tag(i) === "api2 raid") {
          raid = __dados_int_add(raid, 1);
          strength = strength + game.ship_strength(i);
          if (game.ship_government(i) === "Pirate") {
            pirate = __dados_int_add(pirate, 1);
          }
        }
        if (game.ship_tag(i) === "api2 trader" && game.ship_government(i) === "Merchant") {
          trader = __dados_int_add(trader, 1);
        }
      }
    }
    this.rec(game, "seen raid", raid);
    this.rec(game, "seen raid pirate", pirate);
    this.rec(game, "seen trader", trader);
    this.rec(game, "seen raid strength", __dados_int_of_float(strength));
    this.flag(game, "untagged", game.ship_tag(0) === "" && game.ship_tag(-1) === "" && game.ship_government(-1) === "");
  }
  kills(game) {
    this.rec(game, "tag strength after", __dados_int_of_float(game.tag_strength("api2 raid")));
    this.rec(game, "kills api2", game.player_kills("api2"));
    this.rec(game, "kills api2 again", game.player_kills("api2"));
    this.rec(game, "kills raid", game.player_kills("api2 raid"));
  }
  on_day(game, day) {
    if (!this.enabled(game)) {
      return;
    }
    this.rec(game, "days seen", __dados_int_add(game.condition("test: api2 days seen"), 1));
    this.rec(game, "last day", day);
    this.flag(game, "day matches", game.day() === day);
  }
  on_enter(game, system) {
    if (!this.enabled(game)) {
      return;
    }
    this.rec(game, "entered " + system, 1);
    this.flag(game, "here matches", game.here() === system);
    this.rec(game, "ambush " + system, game.spawn("Pirate Raid", "Pirate", "api2 ambush", true));
  }
  on_planet_button(game) {
    if (!this.enabled(game)) {
      return;
    }
    this.rec(game, "planet clicks", __dados_int_add(game.condition("test: api2 planet clicks"), 1));
    this.rec(game, "landed spawn", game.spawn("Pirate Raid", "Pirate", "api2 landed", false));
    this.rec(game, "landed reputation", __dados_int_of_float(game.reputation("Pirate") * 100));
    game.panel_open("Raid board", "Choose a target.");
    game.panel_button("Cancel");
    game.panel_button("Raid Delta");
  }
  on_panel(game, button) {
    if (!this.enabled(game)) {
      return;
    }
    this.panel_clicks = __dados_int_add(this.panel_clicks, 1);
    this.rec(game, "panel clicks", this.panel_clicks);
    this.rec(game, "panel button", button);
    if (this.panel_clicks === 1) {
      game.panel_open("Raid planned", "Delta, at dawn.");
      game.panel_button("Close");
      game.planet_button("");
    } else {
      game.panel_close();
    }
  }
};
__dados_lines("api2_probe/api2_probe.dados", ["main.Api2Probe.name", "main.Api2Probe.enabled", "main.Api2Probe.rec", "main.Api2Probe.flag", "main.Api2Probe.describe", "main.Api2Probe.on_step", "main.Api2Probe.galaxy", "main.Api2Probe.governments", "main.Api2Probe.changes", "main.Api2Probe.spawning", "main.Api2Probe.overlays", "main.Api2Probe.inspect", "main.Api2Probe.kills", "main.Api2Probe.on_day", "main.Api2Probe.on_enter", "main.Api2Probe.on_planet_button", "main.Api2Probe.on_panel"], [
  6,
  0,
  0,
  -1,
  13,
  17,
  5,
  0,
  14,
  0,
  0,
  -1,
  17,
  20,
  5,
  1,
  18,
  0,
  0,
  -1,
  21,
  23,
  5,
  2,
  22,
  0,
  0,
  -1,
  25,
  26,
  5,
  3,
  26,
  0,
  0,
  -1,
  29,
  30,
  5,
  4,
  30,
  31,
  5,
  4,
  31,
  32,
  5,
  4,
  34,
  33,
  9,
  4,
  35,
  34,
  13,
  4,
  36,
  33,
  9,
  4,
  37,
  35,
  9,
  4,
  38,
  32,
  5,
  4,
  40,
  36,
  5,
  4,
  41,
  37,
  5,
  4,
  44,
  38,
  9,
  4,
  45,
  39,
  13,
  4,
  46,
  38,
  9,
  4,
  47,
  40,
  9,
  4,
  48,
  37,
  5,
  4,
  50,
  41,
  5,
  4,
  51,
  0,
  0,
  -1,
  54,
  44,
  5,
  5,
  55,
  45,
  9,
  5,
  56,
  44,
  5,
  5,
  57,
  46,
  5,
  5,
  58,
  47,
  5,
  5,
  59,
  48,
  9,
  5,
  60,
  47,
  5,
  5,
  61,
  49,
  5,
  5,
  62,
  50,
  5,
  5,
  63,
  51,
  9,
  5,
  64,
  52,
  9,
  5,
  65,
  53,
  9,
  5,
  66,
  54,
  9,
  5,
  67,
  55,
  9,
  5,
  68,
  50,
  5,
  5,
  69,
  57,
  9,
  5,
  70,
  50,
  5,
  5,
  71,
  59,
  9,
  5,
  72,
  50,
  5,
  5,
  73,
  0,
  0,
  -1,
  76,
  63,
  5,
  6,
  77,
  64,
  5,
  6,
  78,
  65,
  5,
  6,
  79,
  66,
  5,
  6,
  80,
  67,
  5,
  6,
  81,
  68,
  5,
  6,
  82,
  69,
  5,
  6,
  85,
  70,
  9,
  6,
  86,
  69,
  5,
  6,
  88,
  71,
  5,
  6,
  89,
  72,
  5,
  6,
  90,
  0,
  0,
  -1,
  93,
  79,
  5,
  7,
  94,
  80,
  5,
  7,
  95,
  81,
  5,
  7,
  96,
  82,
  5,
  7,
  97,
  83,
  5,
  7,
  98,
  84,
  5,
  7,
  99,
  86,
  5,
  7,
  100,
  87,
  9,
  7,
  101,
  86,
  5,
  7,
  102,
  88,
  5,
  7,
  103,
  89,
  5,
  7,
  104,
  90,
  9,
  7,
  105,
  91,
  9,
  7,
  108,
  92,
  13,
  7,
  109,
  91,
  9,
  7,
  111,
  93,
  9,
  7,
  112,
  89,
  5,
  7,
  113,
  94,
  5,
  7,
  114,
  0,
  0,
  -1,
  117,
  98,
  5,
  8,
  118,
  99,
  5,
  8,
  119,
  100,
  5,
  8,
  120,
  0,
  0,
  -1,
  123,
  104,
  5,
  9,
  124,
  105,
  5,
  9,
  125,
  106,
  5,
  9,
  126,
  107,
  5,
  9,
  127,
  108,
  5,
  9,
  128,
  0,
  0,
  -1,
  131,
  112,
  5,
  10,
  132,
  113,
  5,
  10,
  133,
  114,
  5,
  10,
  134,
  116,
  5,
  10,
  135,
  117,
  5,
  10,
  136,
  118,
  5,
  10,
  137,
  119,
  5,
  10,
  138,
  120,
  5,
  10,
  139,
  121,
  5,
  10,
  140,
  122,
  5,
  10,
  141,
  123,
  5,
  10,
  142,
  0,
  0,
  -1,
  145,
  127,
  5,
  11,
  146,
  128,
  5,
  11,
  147,
  129,
  5,
  11,
  148,
  130,
  5,
  11,
  149,
  131,
  5,
  11,
  152,
  132,
  9,
  11,
  153,
  133,
  13,
  11,
  154,
  134,
  13,
  11,
  155,
  135,
  13,
  11,
  156,
  136,
  17,
  11,
  157,
  135,
  13,
  11,
  158,
  132,
  9,
  11,
  159,
  137,
  9,
  11,
  160,
  138,
  13,
  11,
  161,
  137,
  9,
  11,
  162,
  131,
  5,
  11,
  164,
  139,
  5,
  11,
  165,
  140,
  5,
  11,
  166,
  141,
  5,
  11,
  167,
  142,
  5,
  11,
  168,
  143,
  5,
  11,
  169,
  0,
  0,
  -1,
  172,
  147,
  5,
  12,
  173,
  148,
  5,
  12,
  174,
  149,
  5,
  12,
  175,
  150,
  5,
  12,
  176,
  0,
  0,
  -1,
  179,
  153,
  5,
  13,
  180,
  154,
  9,
  13,
  181,
  153,
  5,
  13,
  182,
  155,
  5,
  13,
  183,
  156,
  5,
  13,
  184,
  157,
  5,
  13,
  185,
  0,
  0,
  -1,
  188,
  160,
  5,
  14,
  189,
  161,
  9,
  14,
  190,
  160,
  5,
  14,
  191,
  162,
  5,
  14,
  192,
  163,
  5,
  14,
  193,
  164,
  5,
  14,
  194,
  0,
  0,
  -1,
  197,
  167,
  5,
  15,
  198,
  168,
  9,
  15,
  199,
  167,
  5,
  15,
  200,
  169,
  5,
  15,
  201,
  170,
  5,
  15,
  202,
  171,
  5,
  15,
  203,
  172,
  5,
  15,
  204,
  173,
  5,
  15,
  205,
  174,
  5,
  15,
  206,
  0,
  0,
  -1,
  209,
  177,
  5,
  16,
  210,
  178,
  9,
  16,
  211,
  177,
  5,
  16,
  212,
  179,
  5,
  16,
  213,
  180,
  5,
  16,
  214,
  181,
  5,
  16,
  215,
  182,
  5,
  16,
  216,
  183,
  9,
  16,
  217,
  184,
  9,
  16,
  218,
  185,
  9,
  16,
  219,
  182,
  5,
  16,
  220,
  187,
  9,
  16,
  221,
  182,
  5,
  16,
  222,
  0,
  0,
  -1
]);
export {
  Api2Probe
};
