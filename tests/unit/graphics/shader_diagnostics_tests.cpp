#include <catch2/catch_test_macros.hpp>

import GPP;
import std;

using namespace GPP;

TEST_CASE("Shader errors parse file, line and text", "[graphics][shader]")
{
    const auto parsed = ParseShaderError(
        "Shader compilation failed for /s/flat.comp:\n/s/flat.comp:42: error: 'x' : undeclared identifier\n1 error generated.");
    CHECK(parsed.File == "/s/flat.comp");
    CHECK(parsed.Line == 42);
    CHECK(parsed.Text == "error: 'x' : undeclared identifier");

    const auto include = ParseShaderError("ERROR: /s/common.glsl:7: 'foo' : syntax error");
    CHECK(include.File == "/s/common.glsl");
    CHECK(include.Line == 7);

    const auto plain = ParseShaderError("Shader reflection failed for /s/a.comp: bad\nmore");
    CHECK(plain.File.empty());
    CHECK(plain.Line == 0);
    CHECK(plain.Text == "Shader reflection failed for /s/a.comp: bad");
}

TEST_CASE("Shader error description is one short line", "[graphics][shader]")
{
    CHECK(DescribeShaderError("Shader compilation failed for /s/flat.comp:\n/s/flat.comp:42: error: 'x' : bad\n1 error")
          == "flat.comp:42: error: 'x' : bad");
    CHECK(DescribeShaderError("plain failure\nmore") == "plain failure");
}

TEST_CASE("Shader diagnostics keep the latest failure per pipeline", "[graphics][shader]")
{
    ShaderDiagnostics diagnostics;
    CHECK(diagnostics.Failures().empty());
    const auto start = diagnostics.Revision();

    diagnostics.ReportFailure("a#1", "a.comp", "/s/a.comp:3: error: first");
    diagnostics.ReportFailure("b#2", "b.comp", "oops");
    diagnostics.ReportFailure("a#1", "a.comp", "/s/a.comp:9: error: second");
    REQUIRE(diagnostics.Failures().size() == 2);
    CHECK(diagnostics.Failures().front().Pipeline == "a#1");
    CHECK(diagnostics.Failures().front().Line == 9);
    CHECK(diagnostics.Failures().back().File == "b.comp");
    CHECK(diagnostics.FailureCount() == 3);
    CHECK(diagnostics.Revision() > start);

    const auto revision = diagnostics.Revision();
    diagnostics.ReportSuccess("a#1");
    REQUIRE(diagnostics.Failures().size() == 1);
    CHECK(diagnostics.Failures().front().Pipeline == "b#2");
    CHECK(diagnostics.Revision() > revision);
    CHECK(diagnostics.SuccessCount() == 1);

    const auto stable = diagnostics.Revision();
    diagnostics.ReportSuccess("unknown");
    CHECK(diagnostics.Revision() == stable);

    diagnostics.Forget("b#2");
    CHECK(diagnostics.Failures().empty());
}

TEST_CASE("Shader diagnostics are safe under concurrent writers", "[graphics][shader]")
{
    ShaderDiagnostics diagnostics;
    std::vector<std::jthread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&diagnostics, t]
        {
            for (int i = 0; i < 200; ++i)
            {
                const auto id = std::format("p{}", t);
                diagnostics.ReportFailure(id, "x.comp", "/x.comp:1: error");
                (void)diagnostics.Failures();
                if (i % 2 == 0) diagnostics.ReportSuccess(id);
            }
        });
    }
    threads.clear();
    CHECK(diagnostics.FailureCount() == 800);
    CHECK(diagnostics.Failures().size() <= 4);
}
