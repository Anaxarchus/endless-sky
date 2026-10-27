// Asks what a loaded mod object offers, so the harness calls only the hooks it has.
export function has_method(o, name) {
    return o !== null && o !== undefined && typeof o[name] === "function";
}
