#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <string_view>
#include <functional>
#include <sstream>
#include <cstdint>
#include <cstring>

namespace testing {

class Test {
public:
    virtual ~Test() = default;
    virtual void TestBody() = 0;
};

struct TestInfo {
    std::string testCaseName;
    std::string testName;
    std::function<void()> runFunc;
    bool failed{false};
    std::string failureMessage;
};

class UnitTest {
public:
    static UnitTest& GetInstance() {
        static UnitTest instance;
        return instance;
    }

    void RegisterTest(std::string testCaseName, std::string testName, std::function<void()> runFunc) {
        tests_.push_back({std::move(testCaseName), std::move(testName), std::move(runFunc), false, ""});
    }

    bool RunAllTests() {
        size_t passed = 0;
        size_t total = tests_.size();
        std::cout << "[==========] Running " << total << " test(s).\n";

        for (auto& t : tests_) {
            std::cout << "[ RUN      ] " << t.testCaseName << "." << t.testName << "\n";
            currentTest_ = &t;
            try {
                t.runFunc();
            } catch (const std::exception& ex) {
                t.failed = true;
                t.failureMessage += std::string("Unhandled exception: ") + ex.what() + "\n";
            } catch (...) {
                t.failed = true;
                t.failureMessage += "Unhandled non-std exception\n";
            }

            if (t.failed) {
                std::cout << t.failureMessage;
                std::cout << "[  FAILED  ] " << t.testCaseName << "." << t.testName << "\n";
            } else {
                std::cout << "[       OK ] " << t.testCaseName << "." << t.testName << "\n";
                ++passed;
            }
        }

        std::cout << "[==========] " << total << " test(s) run.\n";
        std::cout << "[  PASSED  ] " << passed << " test(s).\n";
        if (passed < total) {
            std::cout << "[  FAILED  ] " << (total - passed) << " test(s).\n";
            return false;
        }
        return true;
    }

    void RecordFailure(const std::string& msg) {
        if (currentTest_) {
            currentTest_->failed = true;
            currentTest_->failureMessage += msg + "\n";
        }
    }

    inline static TestInfo* currentTest_{nullptr};

private:
    std::vector<TestInfo> tests_;
};

inline void InitGoogleTest(int* /*argc*/, char** /*argv*/) {}

class TestRegistrar {
public:
    TestRegistrar(const char* testCaseName, const char* testName, std::function<void()> func) {
        UnitTest::GetInstance().RegisterTest(testCaseName, testName, std::move(func));
    }
};

} // namespace testing

inline int RUN_ALL_TESTS() {
    return ::testing::UnitTest::GetInstance().RunAllTests() ? 0 : 1;
}

#define GTEST_TEST_CLASS_NAME_(test_case_name, test_name) \
    test_case_name##_##test_name##_Test

#define TEST(test_case_name, test_name) \
    class GTEST_TEST_CLASS_NAME_(test_case_name, test_name) : public ::testing::Test { \
    public: \
        void TestBody() override; \
    }; \
    static ::testing::TestRegistrar gtest_reg_##test_case_name##_##test_name( \
        #test_case_name, #test_name, []() { \
            GTEST_TEST_CLASS_NAME_(test_case_name, test_name) t; \
            t.TestBody(); \
        }); \
    void GTEST_TEST_CLASS_NAME_(test_case_name, test_name)::TestBody()

#define EXPECT_EQ(val1, val2) \
    do { \
        if (!((val1) == (val2))) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected equality of: " \
               << #val1 << " == " << #val2; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
        } \
    } while (0)

#define EXPECT_NE(val1, val2) \
    do { \
        if ((val1) == (val2)) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected inequality of: " \
               << #val1 << " != " << #val2; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
        } \
    } while (0)

#define ASSERT_NE(val1, val2) \
    do { \
        if ((val1) == (val2)) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected inequality of: " \
               << #val1 << " != " << #val2; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
            return; \
        } \
    } while (0)

#define ASSERT_EQ(val1, val2) \
    do { \
        if (!((val1) == (val2))) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected equality of: " \
               << #val1 << " == " << #val2; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
            return; \
        } \
    } while (0)

#define EXPECT_LT(val1, val2) \
    do { \
        if (!((val1) < (val2))) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected: (" << #val1 << " < " << #val2 << ")"; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
        } \
    } while (0)

#define EXPECT_LE(val1, val2) \
    do { \
        if (!((val1) <= (val2))) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected: (" << #val1 << " <= " << #val2 << ")"; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
        } \
    } while (0)

#define EXPECT_GT(val1, val2) \
    do { \
        if (!((val1) > (val2))) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected: (" << #val1 << " > " << #val2 << ")"; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
        } \
    } while (0)

#define EXPECT_GE(val1, val2) \
    do { \
        if (!((val1) >= (val2))) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected: (" << #val1 << " >= " << #val2 << ")"; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
        } \
    } while (0)

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected true: " << #cond; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
        } \
    } while (0)

#define EXPECT_FALSE(cond) \
    do { \
        if (cond) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected false: " << #cond; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
        } \
    } while (0)

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected true: " << #cond; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
            return; \
        } \
    } while (0)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected false: " << #cond; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
            return; \
        } \
    } while (0)

#define EXPECT_STRNE(s1, s2) \
    do { \
        if (std::strcmp((s1), (s2)) == 0) { \
            std::ostringstream ss; \
            ss << __FILE__ << ":" << __LINE__ << ": Failure\nExpected inequality of: " \
               << #s1 << " (\"" << (s1) << "\") != " << #s2 << " (\"" << (s2) << "\")"; \
            ::testing::UnitTest::GetInstance().RecordFailure(ss.str()); \
        } \
    } while (0)

namespace testing {

class AssertionHelper {
public:
    template <typename T>
    AssertionHelper& operator<<(const T& val) {
        ss_ << val;
        return *this;
    }
    ~AssertionHelper() {
        ::testing::UnitTest::GetInstance().RecordFailure(ss_.str());
    }
private:
    std::ostringstream ss_;
};

} // namespace testing

#define FAIL() ::testing::AssertionHelper() << __FILE__ << ":" << __LINE__ << ": Failure: "
