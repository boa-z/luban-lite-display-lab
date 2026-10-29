#pragma once
#define MSH_CMD_EXPORT_ALIAS(fn, alias, desc) \
    int bench_command(int argc, char **argv) { return fn(argc, argv); }
