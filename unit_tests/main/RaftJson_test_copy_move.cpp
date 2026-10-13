/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Unit tests of RaftJson copy, move and destruction - documents must stay valid after the source has gone
//
// Rob Dobson 2017-2026
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>
#include "unity.h"
#include "unity_test_runner.h"
#include "RaftArduino.h"
#include "RaftJson.h"
#include "Logger.h"
#include "RaftUtils.h"

// A document which is not copied (as for a document in flash)
static const char* NON_OWNED_JSON = R"({"src":"flash","n":7,"arr":[10,20,30]})";

TEST_CASE("test_raftjson_copy", "[raftjson_copy_move]")
{
    // Copy of an owned document remains valid after the original is destroyed
    std::unique_ptr<RaftJson> pOrig(new RaftJson(R"({"v":42,"s":"text"})"));
    RaftJson copied(*pOrig);
    TEST_ASSERT_TRUE_MESSAGE(copied.getJsonDoc() != pOrig->getJsonDoc(), "copyHasOwnBuffer");
    pOrig.reset();
    TEST_ASSERT_EQUAL_MESSAGE(42, copied.getLong("v", -1), "copyAfterOrigDestroyed");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("text", copied.getString("s", "").c_str(), "copyStringAfterOrigDestroyed");

    // Copy assignment of an owned document
    RaftJson assigned(R"({"old":1})");
    {
        RaftJson source(R"({"v":43})");
        assigned = source;
    }
    TEST_ASSERT_EQUAL_MESSAGE(43, assigned.getLong("v", -1), "copyAssignAfterSourceDestroyed");
    TEST_ASSERT_FALSE_MESSAGE(assigned.contains("old"), "copyAssignReplaces");

    // Self assignment (via a reference to avoid self-assign warnings)
    RaftJson self(R"({"v":44})");
    RaftJson& selfRef = self;
    self = selfRef;
    TEST_ASSERT_EQUAL_MESSAGE(44, self.getLong("v", -1), "selfCopyAssign");

    // Copy of a non-owned document shares the source (no RAM used for the document)
    RaftJson view(NON_OWNED_JSON, false);
    RaftJson viewCopy(view);
    TEST_ASSERT_TRUE_MESSAGE(viewCopy.getJsonDoc() == NON_OWNED_JSON, "viewCopySharesSource");
    TEST_ASSERT_EQUAL_MESSAGE(7, viewCopy.getLong("n", -1), "viewCopyValue");

    // Assigning a non-owned document over an owned one (and the reverse)
    RaftJson ownedThenView(R"({"owned":1})");
    ownedThenView = view;
    TEST_ASSERT_TRUE_MESSAGE(ownedThenView.getJsonDoc() == NON_OWNED_JSON, "assignViewOverOwned");
    TEST_ASSERT_EQUAL_MESSAGE(7, ownedThenView.getLong("n", -1), "assignViewOverOwnedValue");
    RaftJson viewThenOwned(NON_OWNED_JSON, false);
    {
        RaftJson owned(R"({"owned":2})");
        viewThenOwned = owned;
    }
    TEST_ASSERT_EQUAL_MESSAGE(2, viewThenOwned.getLong("owned", -1), "assignOwnedOverView");

    // Copy of an element from an iterator (a view into its parent)
    RaftJson parent(R"({"arr":[{"x":1},{"x":2}]})");
    std::vector<RaftJson> elems;
    for (RaftJson elem : parent.getArray("arr"))
        elems.push_back(elem);
    TEST_ASSERT_EQUAL_MESSAGE(2, elems.size(), "iterElemsCopied");
    TEST_ASSERT_EQUAL_MESSAGE(2, elems[1].getLong("x", -1), "iterElemCopyValue");

    // Chained document is kept by a copy
    RaftJson chained(R"({"fallback":5})");
    RaftJson withChain(R"({"main":1})", true, nullptr, &chained);
    RaftJson chainCopy(withChain);
    TEST_ASSERT_EQUAL_MESSAGE(5, chainCopy.getLong("fallback", -1), "copyKeepsChain");
}

TEST_CASE("test_raftjson_move", "[raftjson_copy_move]")
{
    // Move construction takes the buffer and leaves the source as a valid empty document
    RaftJson source(R"({"v":50})");
    const char* pSourceDoc = source.getJsonDoc();
    RaftJson moved(std::move(source));
    TEST_ASSERT_TRUE_MESSAGE(moved.getJsonDoc() == pSourceDoc, "moveTakesBuffer");
    TEST_ASSERT_EQUAL_MESSAGE(50, moved.getLong("v", -1), "moveValue");
    TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", source.getJsonDoc(), "movedFromIsEmpty");
    TEST_ASSERT_EQUAL_MESSAGE(-1, source.getLong("v", -1), "movedFromNoValue");

    // Move assignment
    RaftJson target(R"({"old":1})");
    target = RaftJson(R"({"v":51})");
    TEST_ASSERT_EQUAL_MESSAGE(51, target.getLong("v", -1), "moveAssignValue");
    TEST_ASSERT_FALSE_MESSAGE(target.contains("old"), "moveAssignReplaces");

    // Self move assignment leaves the document unchanged
    RaftJson self(R"({"v":52})");
    RaftJson& selfRef = self;
    self = std::move(selfRef);
    TEST_ASSERT_EQUAL_MESSAGE(52, self.getLong("v", -1), "selfMoveAssign");

    // Move of a non-owned document
    RaftJson view(NON_OWNED_JSON, false);
    RaftJson movedView(std::move(view));
    TEST_ASSERT_TRUE_MESSAGE(movedView.getJsonDoc() == NON_OWNED_JSON, "moveViewSharesSource");
    TEST_ASSERT_EQUAL_MESSAGE(20, movedView.getLong("arr[1]", -1), "moveViewValue");

    // Documents held in a vector survive the vector growing
    std::vector<RaftJson> docs;
    for (int i = 0; i < 20; i++)
    {
        String jsonStr = String("{\"idx\":") + String(i) + "}";
        docs.push_back(RaftJson(jsonStr));
    }
    for (int i = 0; i < 20; i++)
        TEST_ASSERT_EQUAL_MESSAGE(i, docs[i].getLong("idx", -1), "vectorElemValue");
}

// Derived class which records its destruction
static bool derivedDestroyed = false;
class RaftJsonDestructTest : public RaftJson
{
public:
    RaftJsonDestructTest() : RaftJson(R"({"d":1})") {}
    virtual ~RaftJsonDestructTest() { derivedDestroyed = true; }
};

TEST_CASE("test_raftjson_virtual_destructor", "[raftjson_copy_move]")
{
    TEST_ASSERT_TRUE_MESSAGE(std::has_virtual_destructor<RaftJsonIF>::value, "interfaceHasVirtualDestructor");

    // Deleting through the interface runs the derived destructor
    derivedDestroyed = false;
    RaftJsonIF* pJsonIF = new RaftJsonDestructTest();
    TEST_ASSERT_EQUAL_MESSAGE(1, pJsonIF->getLong("d", -1), "derivedValue");
    delete pJsonIF;
    TEST_ASSERT_TRUE_MESSAGE(derivedDestroyed, "derivedDestructorCalled");
}
