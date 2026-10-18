import QtQuick
import QtTest

// U23d: pb-ui and pb-ui-tests run the JS GC whole (QV4_GC_TIMELIMIT=0) unless the environment says otherwise, as Qt
// 6.11.3's incremental GC crashes under load. gcLimitFound (null when unset) and gcLimit come from tests/main.cpp.
TestCase {
    name: "U23d"

    function test_gc_time_limit() {
        compare(gcLimit, gcLimitFound ?? "0")
    }
}
