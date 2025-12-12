export module test;

import std;
import spdlog;

export void $foo() {
    std::cout << "Test" << std::endl;
    spdlog::info("Test");
    spdlog::sinks::stderr_color_sink_mt asd;
}
