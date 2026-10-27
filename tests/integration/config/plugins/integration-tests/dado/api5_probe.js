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
function __dados_str_int(a) {
  return String(a);
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

// main/Api5Probe.js
var Api5Probe = class extends Mod$class() {
  $fields() {
    super.$fields();
    this.asked = "";
  }
  name() {
    return "api5 probe";
  }
  enabled(game) {
    return game.condition("test: api5") !== 0;
  }
  rec(game, key, value) {
    game.set_condition("test: api5 " + key, value);
  }
  on_enter(game, system) {
    if (!this.enabled(game)) {
      return;
    }
    game.outfitter_button("Integrate");
    this.rec(game, "fleet " + __dados_str_int(game.fleet_count()), 1);
    {
      const end$1 = game.fleet_count();
      for (let k = 0; k < end$1; k = __dados_int_add(k, 1)) {
        let ship = game.fleet_ship(k);
        this.rec(game, "ship " + __dados_str_int(k) + " " + ship + " " + game.fleet_ship_name(ship) + " " + game.fleet_ship_model(ship), 1);
      }
    }
    this.rec(game, "fleet out of range", game.fleet_ship(99) === "" && game.fleet_ship(-1) === "" ? 1 : 0);
    this.rec(game, "laser " + game.outfit_name("Laser") + " " + game.outfit_category("Laser"), 1);
    this.rec(game, "laser mass", __dados_int_of_float(game.outfit_attribute("Laser", "mass")));
    this.rec(game, "laser cost", __dados_int_of_float(game.outfit_attribute("Laser", "cost")));
    this.rec(game, "battery energy", __dados_int_of_float(game.outfit_attribute("Battery", "energy capacity")));
    this.rec(game, "laser weapon", game.outfit_is_weapon("Laser") ? 1 : 0);
    this.rec(game, "battery weapon", game.outfit_is_weapon("Battery") ? 1 : 0);
    this.rec(game, "unknown outfit", game.outfit_name("Nope") === "" && game.outfit_attribute("Nope", "mass") === 0 ? 1 : 0);
    this.rec(game, "no outfitter ships", game.outfitter_ship_count() === 0 && game.outfitter_ship(0) === "" ? 1 : 0);
    game.confirm("Too early", 1);
  }
  can_outfitter(game, outfit) {
    if (!this.enabled(game)) {
      return false;
    }
    this.rec(game, "can asked", __dados_int_add(game.condition("test: api5 can asked"), 1));
    this.rec(game, "selected " + __dados_str_int(game.outfitter_ship_count()), 1);
    if (outfit === "" || game.outfitter_ship_count() === 0) {
      return false;
    }
    let ship = game.outfitter_ship(0);
    return game.ship_outfit_count(ship, outfit) > game.ship_integrated_count(ship, outfit);
  }
  on_outfitter(game, outfit) {
    if (!this.enabled(game)) {
      return;
    }
    this.asked = outfit;
    this.rec(game, "clicked " + outfit, 1);
    game.confirm("Integrate the " + game.outfit_name(outfit) + "?", 7);
  }
  on_confirm(game, token) {
    if (!this.enabled(game)) {
      return;
    }
    this.rec(game, "confirmed", token);
    let ship = game.outfitter_ship(0);
    let ok = game.ship_integrate(ship, this.asked, 0.75);
    this.rec(game, "integrated " + this.asked, ok ? 1 : 0);
    this.rec(game, "integrated count " + this.asked, game.ship_integrated_count(ship, this.asked));
    this.rec(game, "ammo refused", !game.ship_integrate(ship, "Gat Ammo", 0.75) ? 1 : 0);
    this.rec(game, "bad scale refused", !game.ship_integrate(ship, this.asked, 1.5) ? 1 : 0);
  }
};
__dados_lines("api5_probe/api5_probe.dados", ["main.Api5Probe.name", "main.Api5Probe.enabled", "main.Api5Probe.rec", "main.Api5Probe.on_enter", "main.Api5Probe.can_outfitter", "main.Api5Probe.on_outfitter", "main.Api5Probe.on_confirm"], [
  6,
  0,
  0,
  -1,
  13,
  15,
  5,
  0,
  14,
  0,
  0,
  -1,
  17,
  18,
  5,
  1,
  18,
  0,
  0,
  -1,
  21,
  21,
  5,
  2,
  22,
  0,
  0,
  -1,
  25,
  24,
  5,
  3,
  26,
  25,
  9,
  3,
  27,
  24,
  5,
  3,
  28,
  26,
  5,
  3,
  29,
  27,
  5,
  3,
  30,
  28,
  5,
  3,
  33,
  29,
  9,
  3,
  34,
  30,
  9,
  3,
  35,
  28,
  5,
  3,
  37,
  31,
  5,
  3,
  38,
  32,
  5,
  3,
  39,
  33,
  5,
  3,
  40,
  34,
  5,
  3,
  41,
  35,
  5,
  3,
  42,
  36,
  5,
  3,
  43,
  37,
  5,
  3,
  44,
  38,
  5,
  3,
  45,
  39,
  5,
  3,
  46,
  41,
  5,
  3,
  47,
  0,
  0,
  -1,
  50,
  44,
  5,
  4,
  51,
  45,
  9,
  4,
  52,
  44,
  5,
  4,
  53,
  46,
  5,
  4,
  54,
  47,
  5,
  4,
  55,
  48,
  5,
  4,
  56,
  49,
  9,
  4,
  57,
  48,
  5,
  4,
  58,
  50,
  5,
  4,
  59,
  51,
  5,
  4,
  60,
  0,
  0,
  -1,
  63,
  54,
  5,
  5,
  64,
  55,
  9,
  5,
  65,
  54,
  5,
  5,
  66,
  56,
  5,
  5,
  67,
  57,
  5,
  5,
  68,
  58,
  5,
  5,
  69,
  0,
  0,
  -1,
  72,
  61,
  5,
  6,
  73,
  62,
  9,
  6,
  74,
  61,
  5,
  6,
  75,
  63,
  5,
  6,
  76,
  64,
  5,
  6,
  77,
  65,
  5,
  6,
  78,
  66,
  5,
  6,
  79,
  67,
  5,
  6,
  80,
  68,
  5,
  6,
  81,
  69,
  5,
  6,
  82,
  0,
  0,
  -1
]);
export {
  Api5Probe
};
