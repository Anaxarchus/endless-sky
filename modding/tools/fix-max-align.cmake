# Works around a Dado 1.0.0-rc.8 bug in the C it emits (run as `cmake -P`, with
# -DFILE=<esmod.c>). The emitted runtime works out the platform's largest
# alignment from `long double`, which on Apple ARM is 8 bytes, while the
# standard library asks the heap for mem.MAX_ALIGN (16). The C heap then refuses
# every request: the script engine reports "script: no memory for a VM" and
# the harness does not start. Every platform the game runs on hands out 16-byte
# aligned memory from malloc, so the alignment is pinned to 16. Once Dado emits
# a correct value, the line is no longer there and this does nothing.
file(READ "${FILE}" _text)
set(_bad "#define DADO_MAX_ALIGN ((uint64_t)offsetof(struct dado_rt__align_probe, t))")
string(FIND "${_text}" "${_bad}" _at)
if(NOT _at EQUAL -1)
	string(REPLACE "${_bad}" "#define DADO_MAX_ALIGN ((uint64_t)16) /* see modding/tools/fix-max-align.cmake */" _text "${_text}")
	file(WRITE "${FILE}" "${_text}")
endif()
