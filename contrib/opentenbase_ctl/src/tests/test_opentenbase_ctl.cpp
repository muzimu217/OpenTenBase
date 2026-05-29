/**
 * Unit tests for opentenbase_ctl pure/utility functions.
 *
 * These tests cover the functions refactored in commits 9c0fc58c and b612d77c
 * to verify that no behavior changed.  They intentionally avoid anything that
 * requires SSH or a running cluster so they can execute in any CI environment.
 */

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

/* Project headers */
#include "../utils/utils.h"
#include "../types/types.h"
#include "../config/config.h"
#include "../command/command.h"
#include "../file/file.h"
#include "../log/log.h"

/* Forward declarations for internal-linkage helpers in types.cpp */
bool is_valid_ip_address(const std::string& ip);
bool isNullOrEmptyOrWhitespace(const std::string& str);
int  get_nodes_per_servers(const std::string& str);
int  get_ssh_port(const std::string& str);
std::string get_package_name(const std::string& full_path_pkg);
std::string get_prefix_by_node_type(const std::string type);
std::string toUpperCase(const std::string& input);
bool startsWith(const std::string& str, const std::string& prefix);
bool parseIpPort(const std::string& ip_port, std::string& ip, int& port);
int  generate_nodes(const std::vector<std::string>& ips, int nodes_per_server,
                    int rounds_num, const std::string type,
                    OpentenbaseConfig& config);

/* ------------------------------------------------------------------ */
/*  Minimal test harness                                               */
/* ------------------------------------------------------------------ */
static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name)                                                        \
    static void test_##name();                                            \
    static struct Register_##name {                                       \
        Register_##name() { register_test(#name, test_##name); }          \
    } reg_##name;                                                         \
    static void test_##name()

struct TestEntry { const char* name; void (*fn)(); };
static std::vector<TestEntry>& test_registry() {
    static std::vector<TestEntry> v;
    return v;
}
static void register_test(const char* name, void (*fn)()) {
    test_registry().push_back({name, fn});
}

#define EXPECT_EQ(a, b) do {                                              \
    auto _a = (a); auto _b = (b);                                        \
    if (_a != _b) {                                                       \
        std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__             \
                  << ": expected (" << #a << " == " << #b << ") got ["    \
                  << _a << "] vs [" << _b << "]\n";                       \
        throw std::runtime_error("assertion failed");                     \
    }                                                                     \
} while (0)

#define EXPECT_TRUE(x)  EXPECT_EQ(!!(x), true)
#define EXPECT_FALSE(x) EXPECT_EQ(!!(x), false)

/* ================================================================== */
/*  utils.h tests                                                      */
/* ================================================================== */

/* --- extract_version_from_package_name --- */
TEST(extract_version_basic) {
    EXPECT_EQ(extract_version_from_package_name("opentenbase-5.21.8.tar.gz"),
              std::string("5.21.8"));
}
TEST(extract_version_long) {
    EXPECT_EQ(extract_version_from_package_name("opentenbase-3.16.9.301.tar.gz"),
              std::string("3.16.9.301"));
}
TEST(extract_version_rpm) {
    EXPECT_EQ(extract_version_from_package_name("opentenbase-5.06.1.rpm"),
              std::string("5.06.1"));
}
TEST(extract_version_no_match) {
    EXPECT_EQ(extract_version_from_package_name("noversion.tar.gz"),
              std::string(""));
}
TEST(extract_version_empty) {
    EXPECT_EQ(extract_version_from_package_name(""), std::string(""));
}
TEST(extract_version_single_number) {
    // Single number after dash — not multiple dot-separated segments
    EXPECT_EQ(extract_version_from_package_name("pkg-123"), std::string(""));
}

/* --- get_value_after_equal --- */
TEST(get_value_after_equal_basic) {
    EXPECT_EQ(get_value_after_equal("port = 5432"), std::string("5432"));
}
TEST(get_value_after_equal_no_equal) {
    EXPECT_EQ(get_value_after_equal("no_equal_sign"), std::string(""));
}
TEST(get_value_after_equal_trailing_comment) {
    EXPECT_EQ(get_value_after_equal("port = 5432 # comment"),
              std::string("5432"));
}
TEST(get_value_after_equal_empty_value) {
    EXPECT_EQ(get_value_after_equal("key="), std::string(""));
}
TEST(get_value_after_equal_spaces_only) {
    EXPECT_EQ(get_value_after_equal("key=   "), std::string(""));
}
TEST(get_value_after_equal_tabs) {
    EXPECT_EQ(get_value_after_equal("key\t=\t8080\t"), std::string("8080"));
}

/* --- is_rpm_package --- */
TEST(is_rpm_package_true) {
    EXPECT_TRUE(is_rpm_package("opentenbase-5.21.8.rpm"));
}
TEST(is_rpm_package_case_insensitive) {
    EXPECT_TRUE(is_rpm_package("PKG.RPM"));
}
TEST(is_rpm_package_false_tar) {
    EXPECT_FALSE(is_rpm_package("opentenbase-5.21.8.tar.gz"));
}
TEST(is_rpm_package_too_short) {
    EXPECT_FALSE(is_rpm_package(".rp"));
}
TEST(is_rpm_package_empty) {
    EXPECT_FALSE(is_rpm_package(""));
}

/* --- Node type predicates --- */
TEST(is_master_gtm_true) {
    EXPECT_TRUE(is_master_gtm("gtm_master"));
}
TEST(is_master_gtm_false) {
    EXPECT_FALSE(is_master_gtm("gtm_slave"));
    EXPECT_FALSE(is_master_gtm("cn_master"));
    EXPECT_FALSE(is_master_gtm(""));
}
TEST(is_slave_gtm_true) {
    EXPECT_TRUE(is_slave_gtm("gtm_slave"));
}
TEST(is_slave_gtm_false) {
    EXPECT_FALSE(is_slave_gtm("gtm_master"));
}
TEST(is_gtm_node_both) {
    EXPECT_TRUE(is_gtm_node("gtm_master"));
    EXPECT_TRUE(is_gtm_node("gtm_slave"));
    EXPECT_FALSE(is_gtm_node("cn_master"));
}
TEST(is_master_cn_true) {
    EXPECT_TRUE(is_master_cn("cn_master"));
}
TEST(is_master_cn_false) {
    EXPECT_FALSE(is_master_cn("cn_slave"));
    EXPECT_FALSE(is_master_cn("dn_master"));
}
TEST(is_slave_cn_true) {
    EXPECT_TRUE(is_slave_cn("cn_slave"));
}
TEST(is_slave_cn_false) {
    EXPECT_FALSE(is_slave_cn("cn_master"));
}
TEST(is_cn_node_both) {
    EXPECT_TRUE(is_cn_node("cn_master"));
    EXPECT_TRUE(is_cn_node("cn_slave"));
    EXPECT_FALSE(is_cn_node("dn_master"));
}
TEST(is_master_dn_true) {
    EXPECT_TRUE(is_master_dn("dn_master"));
}
TEST(is_master_dn_false) {
    EXPECT_FALSE(is_master_dn("dn_slave"));
    EXPECT_FALSE(is_master_dn("cn_master"));
}
TEST(is_slave_dn_true) {
    EXPECT_TRUE(is_slave_dn("dn_slave"));
}
TEST(is_slave_dn_false) {
    EXPECT_FALSE(is_slave_dn("dn_master"));
}
TEST(is_dn_node_both) {
    EXPECT_TRUE(is_dn_node("dn_master"));
    EXPECT_TRUE(is_dn_node("dn_slave"));
    EXPECT_FALSE(is_dn_node("cn_master"));
}
TEST(is_master_node_all) {
    EXPECT_TRUE(is_master_node("cn_master"));
    EXPECT_TRUE(is_master_node("dn_master"));
    EXPECT_TRUE(is_master_node("gtm_master"));
    EXPECT_FALSE(is_master_node("cn_slave"));
    EXPECT_FALSE(is_master_node("dn_slave"));
    EXPECT_FALSE(is_master_node("gtm_slave"));
    EXPECT_FALSE(is_master_node(""));
}

/* --- is_fusion_version --- */
TEST(is_fusion_version_521) {
    EXPECT_TRUE(is_fusion_version("5.21.8"));
}
TEST(is_fusion_version_316) {
    EXPECT_TRUE(is_fusion_version("3.16.9.301"));
}
TEST(is_fusion_version_old) {
    EXPECT_FALSE(is_fusion_version("2.15.1"));
    EXPECT_FALSE(is_fusion_version("5.06.1"));
    EXPECT_FALSE(is_fusion_version("5.05.0"));
}
TEST(is_fusion_version_short) {
    EXPECT_FALSE(is_fusion_version("5.2"));
    EXPECT_FALSE(is_fusion_version(""));
}

/* --- is_Centralized_instance --- */
TEST(is_centralized_true) {
    EXPECT_TRUE(is_Centralized_instance("centralized"));
}
TEST(is_centralized_false) {
    EXPECT_FALSE(is_Centralized_instance("distributed"));
    EXPECT_FALSE(is_Centralized_instance(""));
}

/* --- escape_sql --- */
TEST(escape_sql_no_special) {
    EXPECT_EQ(escape_sql("SELECT 1"), std::string("SELECT 1"));
}
TEST(escape_sql_double_quote) {
    EXPECT_EQ(escape_sql("say \"hello\""),
              std::string("say \\\"hello\\\""));
}
TEST(escape_sql_backslash) {
    EXPECT_EQ(escape_sql("path\\dir"), std::string("path\\\\dir"));
}
TEST(escape_sql_mixed) {
    EXPECT_EQ(escape_sql("a\"b\\c"),
              std::string("a\\\"b\\\\c"));
}
TEST(escape_sql_empty) {
    EXPECT_EQ(escape_sql(""), std::string(""));
}

/* --- buid_ld_library_path_str --- */
TEST(build_ld_library_path) {
    std::string result = buid_ld_library_path_str("/usr/local/bin");
    EXPECT_TRUE(result.find("LD_LIBRARY_PATH=/usr/local/bin/lib") != std::string::npos);
    EXPECT_TRUE(result.find("PATH=/usr/local/bin/bin") != std::string::npos);
}

/* ================================================================== */
/*  types.h / types.cpp tests                                          */
/* ================================================================== */

/* --- get_node_name --- */
TEST(get_node_name_cn) {
    EXPECT_EQ(get_node_name("cn001"),  std::string("cn001"));
    EXPECT_EQ(get_node_name("cn0001"), std::string("cn0001"));
}
TEST(get_node_name_dn) {
    EXPECT_EQ(get_node_name("dn001"),  std::string("dn001"));
    EXPECT_EQ(get_node_name("dn00001"), std::string("dn00001"));
}
TEST(get_node_name_gtm_increment) {
    EXPECT_EQ(get_node_name("gtm001"),  std::string("gtm002"));
    EXPECT_EQ(get_node_name("gtm009"),  std::string("gtm010"));
    EXPECT_EQ(get_node_name("gtm0001"), std::string("gtm0002"));
    EXPECT_EQ(get_node_name("gtm00001"), std::string("gtm00002"));
}
TEST(get_node_name_invalid) {
    EXPECT_EQ(get_node_name(""),       std::string(""));
    EXPECT_EQ(get_node_name("xyz001"), std::string(""));
    EXPECT_EQ(get_node_name("gtm"),    std::string(""));
    EXPECT_EQ(get_node_name("cn"),     std::string(""));
}
TEST(get_node_name_gtm_non_digit) {
    EXPECT_EQ(get_node_name("gtm00a1"), std::string(""));
}

/* --- is_valid_ip_address (forward-declared) --- */
TEST(valid_ipv4) {
    EXPECT_TRUE(is_valid_ip_address("192.168.1.1"));
    EXPECT_TRUE(is_valid_ip_address("0.0.0.0"));
    EXPECT_TRUE(is_valid_ip_address("255.255.255.255"));
    EXPECT_TRUE(is_valid_ip_address("10.0.0.1"));
}
TEST(invalid_ipv4) {
    EXPECT_FALSE(is_valid_ip_address("256.1.1.1"));
    EXPECT_FALSE(is_valid_ip_address("1.2.3"));
    EXPECT_FALSE(is_valid_ip_address("abc.def.ghi.jkl"));
    EXPECT_FALSE(is_valid_ip_address(""));
}

/* --- isNullOrEmptyOrWhitespace --- */
TEST(null_empty_whitespace_true) {
    EXPECT_TRUE(isNullOrEmptyOrWhitespace(""));
    EXPECT_TRUE(isNullOrEmptyOrWhitespace("   "));
    EXPECT_TRUE(isNullOrEmptyOrWhitespace("\t\n"));
}
TEST(null_empty_whitespace_false) {
    EXPECT_FALSE(isNullOrEmptyOrWhitespace("a"));
    EXPECT_FALSE(isNullOrEmptyOrWhitespace(" a "));
}

/* --- get_nodes_per_servers --- */
TEST(get_nodes_per_servers_normal) {
    EXPECT_EQ(get_nodes_per_servers("3"), 3);
    EXPECT_EQ(get_nodes_per_servers("1"), 1);
}
TEST(get_nodes_per_servers_empty) {
    EXPECT_EQ(get_nodes_per_servers(""), 1);
    EXPECT_EQ(get_nodes_per_servers("   "), 1);
}
TEST(get_nodes_per_servers_invalid) {
    EXPECT_EQ(get_nodes_per_servers("abc"), -1);
}

/* --- get_ssh_port --- */
TEST(get_ssh_port_normal) {
    EXPECT_EQ(get_ssh_port("22"), 22);
    EXPECT_EQ(get_ssh_port("2222"), 2222);
}
TEST(get_ssh_port_invalid) {
    EXPECT_EQ(get_ssh_port("abc"), -1);
}

/* --- get_package_name --- */
TEST(get_package_name_with_path) {
    EXPECT_EQ(get_package_name("/tmp/packages/opentenbase-5.21.8.tar.gz"),
              std::string("opentenbase-5.21.8.tar.gz"));
}
TEST(get_package_name_no_slash) {
    EXPECT_EQ(get_package_name("opentenbase-5.21.8.tar.gz"),
              std::string("opentenbase-5.21.8.tar.gz"));
}
TEST(get_package_name_windows_path) {
    EXPECT_EQ(get_package_name("C:\\packages\\pkg.tar.gz"),
              std::string("pkg.tar.gz"));
}

/* --- get_prefix_by_node_type --- */
TEST(get_prefix_cn) {
    EXPECT_EQ(get_prefix_by_node_type("cn_master"), std::string("cn"));
    EXPECT_EQ(get_prefix_by_node_type("cn_slave"),  std::string("cn"));
}
TEST(get_prefix_dn) {
    EXPECT_EQ(get_prefix_by_node_type("dn_master"), std::string("dn"));
    EXPECT_EQ(get_prefix_by_node_type("dn_slave"),  std::string("dn"));
}
TEST(get_prefix_gtm) {
    EXPECT_EQ(get_prefix_by_node_type("gtm_master"), std::string("gtm"));
    EXPECT_EQ(get_prefix_by_node_type("gtm_slave"),  std::string("gtm"));
}
TEST(get_prefix_unknown) {
    EXPECT_EQ(get_prefix_by_node_type("xyz"), std::string(""));
}

/* --- toUpperCase --- */
TEST(to_upper_basic) {
    EXPECT_EQ(toUpperCase("debug"), std::string("DEBUG"));
    EXPECT_EQ(toUpperCase("Info"),  std::string("INFO"));
    EXPECT_EQ(toUpperCase("ERROR"), std::string("ERROR"));
    EXPECT_EQ(toUpperCase(""),      std::string(""));
}

/* --- startsWith --- */
TEST(starts_with_true) {
    EXPECT_TRUE(startsWith("cn0001", "cn"));
    EXPECT_TRUE(startsWith("dn0001", "dn"));
    EXPECT_TRUE(startsWith("gtm0001", "gtm"));
}
TEST(starts_with_false) {
    EXPECT_FALSE(startsWith("cn0001", "dn"));
    EXPECT_FALSE(startsWith("", "cn"));
}

/* --- parseIpPort --- */
TEST(parse_ip_port_valid) {
    std::string ip; int port;
    EXPECT_TRUE(parseIpPort("192.168.1.1:5432", ip, port));
    EXPECT_EQ(ip, std::string("192.168.1.1"));
    EXPECT_EQ(port, 5432);
}
TEST(parse_ip_port_invalid_no_colon) {
    std::string ip; int port;
    EXPECT_FALSE(parseIpPort("192.168.1.1", ip, port));
}
TEST(parse_ip_port_invalid_port) {
    std::string ip; int port;
    EXPECT_FALSE(parseIpPort("192.168.1.1:abc", ip, port));
}

/* --- process_op_nodes --- */
TEST(process_op_nodes_install_noop) {
    CommandLineArgs args;
    args.command = "install";
    args.op_node = "";
    OpentenbaseConfig config;
    NodeInfo n; n.name = "cn0001"; n.type = "cn_master"; n.ip = "1.1.1.1"; n.port = 5432; n.is_op_node = true;
    config.nodes.push_back(n);
    EXPECT_EQ(process_op_nodes(args, config), 0);
    EXPECT_TRUE(config.nodes[0].is_op_node);
}
TEST(process_op_nodes_cn_master_filter) {
    CommandLineArgs args;
    args.command = "start";
    args.op_node = "cn-master";
    OpentenbaseConfig config;
    NodeInfo n1; n1.name = "cn0001"; n1.type = "cn_master"; n1.ip = "1.1.1.1"; n1.port = 5432;
    NodeInfo n2; n2.name = "dn0001"; n2.type = "dn_master"; n2.ip = "1.1.1.2"; n2.port = 5433;
    config.nodes.push_back(n1);
    config.nodes.push_back(n2);
    EXPECT_EQ(process_op_nodes(args, config), 0);
    EXPECT_TRUE(config.nodes[0].is_op_node);
    EXPECT_FALSE(config.nodes[1].is_op_node);
}
TEST(process_op_nodes_dn_slave_filter) {
    CommandLineArgs args;
    args.command = "stop";
    args.op_node = "dn-slave";
    OpentenbaseConfig config;
    NodeInfo n1; n1.name = "dn0001"; n1.type = "dn_master"; n1.ip = "1.1.1.1"; n1.port = 5432;
    NodeInfo n2; n2.name = "dn0002"; n2.type = "dn_slave"; n2.ip = "1.1.1.2"; n2.port = 5433;
    config.nodes.push_back(n1);
    config.nodes.push_back(n2);
    EXPECT_EQ(process_op_nodes(args, config), 0);
    EXPECT_FALSE(config.nodes[0].is_op_node);
    EXPECT_TRUE(config.nodes[1].is_op_node);
}
TEST(process_op_nodes_by_name) {
    CommandLineArgs args;
    args.command = "status";
    args.op_node = "cn0002";
    OpentenbaseConfig config;
    NodeInfo n1; n1.name = "cn0001"; n1.type = "cn_master"; n1.ip = "1.1.1.1"; n1.port = 5432;
    NodeInfo n2; n2.name = "cn0002"; n2.type = "cn_master"; n2.ip = "1.1.1.2"; n2.port = 5433;
    config.nodes.push_back(n1);
    config.nodes.push_back(n2);
    EXPECT_EQ(process_op_nodes(args, config), 0);
    EXPECT_FALSE(config.nodes[0].is_op_node);
    EXPECT_TRUE(config.nodes[1].is_op_node);
}
TEST(process_op_nodes_by_ip_port) {
    CommandLineArgs args;
    args.command = "status";
    args.op_node = "10.0.0.1:5432";
    OpentenbaseConfig config;
    NodeInfo n1; n1.name = "cn0001"; n1.type = "cn_master"; n1.ip = "10.0.0.1"; n1.port = 5432;
    NodeInfo n2; n2.name = "cn0002"; n2.type = "cn_master"; n2.ip = "10.0.0.2"; n2.port = 5433;
    config.nodes.push_back(n1);
    config.nodes.push_back(n2);
    EXPECT_EQ(process_op_nodes(args, config), 0);
    EXPECT_TRUE(config.nodes[0].is_op_node);
    EXPECT_FALSE(config.nodes[1].is_op_node);
}
TEST(process_op_nodes_invalid_string) {
    CommandLineArgs args;
    args.command = "status";
    args.op_node = "invalid_string";
    OpentenbaseConfig config;
    EXPECT_EQ(process_op_nodes(args, config), -1);
}

/* --- generate_nodes --- */
TEST(generate_nodes_basic) {
    OpentenbaseConfig config;
    std::vector<std::string> ips = {"10.0.0.1", "10.0.0.2"};
    EXPECT_EQ(generate_nodes(ips, 1, 2, "cn_master", config), 0);
    EXPECT_EQ(config.nodes.size(), (size_t)2);
    EXPECT_EQ(config.nodes[0].name, std::string("cn0001"));
    EXPECT_EQ(config.nodes[0].ip,   std::string("10.0.0.1"));
    EXPECT_EQ(config.nodes[1].name, std::string("cn0002"));
    EXPECT_EQ(config.nodes[1].ip,   std::string("10.0.0.2"));
}
TEST(generate_nodes_multi_per_server) {
    OpentenbaseConfig config;
    std::vector<std::string> ips = {"10.0.0.1"};
    EXPECT_EQ(generate_nodes(ips, 3, 3, "dn_master", config), 0);
    EXPECT_EQ(config.nodes.size(), (size_t)3);
    EXPECT_EQ(config.nodes[0].name, std::string("dn0001"));
    EXPECT_EQ(config.nodes[1].name, std::string("dn0002"));
    EXPECT_EQ(config.nodes[2].name, std::string("dn0003"));
}
TEST(generate_nodes_invalid_rounds) {
    OpentenbaseConfig config;
    std::vector<std::string> ips = {"10.0.0.1"};
    EXPECT_EQ(generate_nodes(ips, 1, 0, "cn_master", config), -1);
    EXPECT_EQ(generate_nodes(ips, 1, -1, "cn_master", config), -1);
}

/* --- build_scp_config --- */
TEST(build_scp_config_basic) {
    CommandLineArgs args;
    args.source_file = "/tmp/file.tar.gz";
    args.dest_path   = "/opt/remote/";
    OpentenbaseConfig config;
    EXPECT_EQ(build_scp_config(args, config), 0);
    EXPECT_EQ(config.scpfile.source_file, std::string("/tmp/file.tar.gz"));
    EXPECT_EQ(config.scpfile.dest_path,   std::string("/opt/remote/"));
}

/* --- build_shell_config --- */
TEST(build_shell_config_basic) {
    CommandLineArgs args;
    args.shell_cmd = "ls -la /tmp";
    OpentenbaseConfig config;
    EXPECT_EQ(build_shell_config(args, config), 0);
    EXPECT_EQ(config.shell.shell_cmd, std::string("ls -la /tmp"));
}

/* --- build_sql_config --- */
TEST(build_sql_config_defaults) {
    CommandLineArgs args;
    args.sql      = "SELECT 1";
    args.user     = "";
    args.database = "";
    OpentenbaseConfig config;
    EXPECT_EQ(build_sql_config(args, config), 0);
    EXPECT_EQ(config.sql.sql, std::string("SELECT 1"));
    EXPECT_EQ(config.sql.user_name,     std::string(Constants::DEFAULT_USER_OF_INITDB));
    EXPECT_EQ(config.sql.database_name, std::string(Constants::DEFAULT_DB));
}
TEST(build_sql_config_custom) {
    CommandLineArgs args;
    args.sql      = "SELECT 2";
    args.user     = "myuser";
    args.database = "mydb";
    OpentenbaseConfig config;
    EXPECT_EQ(build_sql_config(args, config), 0);
    EXPECT_EQ(config.sql.user_name,     std::string("myuser"));
    EXPECT_EQ(config.sql.database_name, std::string("mydb"));
}

/* --- build_guc_config --- */
TEST(build_guc_config_basic) {
    CommandLineArgs args;
    args.guc_key   = "shared_buffers";
    args.guc_value = "256MB";
    args.guc_op    = "change";
    OpentenbaseConfig config;
    EXPECT_EQ(build_guc_config(args, config), 0);
    EXPECT_EQ(config.guc.guc_name,  std::string("shared_buffers"));
    EXPECT_EQ(config.guc.guc_value, std::string("256MB"));
    EXPECT_EQ(config.guc.op_name,   std::string("change"));
}

/* --- fill_node_with_gtm_info --- */
TEST(fill_node_with_gtm_info_found) {
    OpentenbaseConfig config;
    NodeInfo gtm;  gtm.name = "gtm0001"; gtm.type = "gtm_master"; gtm.ip = "10.0.0.1"; gtm.port = 6666;
    NodeInfo cn;   cn.name  = "cn0001";  cn.type  = "cn_master";  cn.ip  = "10.0.0.2"; cn.port  = 5432;
    config.nodes.push_back(gtm);
    config.nodes.push_back(cn);
    EXPECT_EQ(fill_node_with_gtm_info(config), 0);
    EXPECT_EQ(config.nodes[1].gtm_name, std::string("gtm0001"));
    EXPECT_EQ(config.nodes[1].gtm_ip,   std::string("10.0.0.1"));
    EXPECT_EQ(config.nodes[1].gtm_port, 6666);
}
TEST(fill_node_with_gtm_info_not_found) {
    OpentenbaseConfig config;
    NodeInfo cn; cn.name = "cn0001"; cn.type = "cn_master"; cn.ip = "10.0.0.2"; cn.port = 5432;
    config.nodes.push_back(cn);
    EXPECT_EQ(fill_node_with_gtm_info(config), -1);
}

/* --- fill_ports_for_nodes (nullptr check) --- */
TEST(fill_ports_nullptr) {
    EXPECT_EQ(fill_ports_for_nodes(nullptr), -1);
}

/* ================================================================== */
/*  config.h tests                                                     */
/* ================================================================== */

/* --- trim_whitespace --- */
TEST(trim_whitespace_basic) {
    std::string s = "  hello  ";
    trim_whitespace(s);
    EXPECT_EQ(s, std::string("hello"));
}
TEST(trim_whitespace_tabs) {
    std::string s = "\t\nhello\r\n";
    trim_whitespace(s);
    EXPECT_EQ(s, std::string("hello"));
}
TEST(trim_whitespace_empty) {
    std::string s = "   ";
    trim_whitespace(s);
    EXPECT_EQ(s, std::string(""));
}
TEST(trim_whitespace_noop) {
    std::string s = "hello";
    trim_whitespace(s);
    EXPECT_EQ(s, std::string("hello"));
}

/* --- parse_node_list --- */
TEST(parse_node_list_basic) {
    auto v = parse_node_list("cn0001,cn0002,dn0001");
    EXPECT_EQ(v.size(), (size_t)3);
    EXPECT_EQ(v[0], std::string("cn0001"));
    EXPECT_EQ(v[1], std::string("cn0002"));
    EXPECT_EQ(v[2], std::string("dn0001"));
}
TEST(parse_node_list_with_spaces) {
    auto v = parse_node_list(" cn0001 , cn0002 ");
    EXPECT_EQ(v.size(), (size_t)2);
    EXPECT_EQ(v[0], std::string("cn0001"));
    EXPECT_EQ(v[1], std::string("cn0002"));
}
TEST(parse_node_list_single) {
    auto v = parse_node_list("dn0001");
    EXPECT_EQ(v.size(), (size_t)1);
    EXPECT_EQ(v[0], std::string("dn0001"));
}

/* --- infer_node_type --- */
TEST(infer_node_type_cn) {
    EXPECT_EQ(infer_node_type("cn0001"), std::string("cn_master"));
}
TEST(infer_node_type_dn) {
    EXPECT_EQ(infer_node_type("dn0001"), std::string("dn_master"));
}
TEST(infer_node_type_gtm) {
    EXPECT_EQ(infer_node_type("gtm0001"), std::string("gtm_master"));
}
TEST(infer_node_type_unknown) {
    EXPECT_EQ(infer_node_type("xyz"), std::string(""));
    EXPECT_EQ(infer_node_type(""),    std::string(""));
    EXPECT_EQ(infer_node_type("a"),   std::string(""));
}

/* --- infer_slave_node_type --- */
TEST(infer_slave_cn) {
    EXPECT_EQ(infer_slave_node_type("cn0001"), std::string("cn_slave"));
}
TEST(infer_slave_dn) {
    EXPECT_EQ(infer_slave_node_type("dn0001"), std::string("dn_slave"));
}
TEST(infer_slave_gtm) {
    EXPECT_EQ(infer_slave_node_type("gtm0001"), std::string("gtm_slave"));
}
TEST(infer_slave_unknown) {
    EXPECT_EQ(infer_slave_node_type("xyz"), std::string(""));
    EXPECT_EQ(infer_slave_node_type(""),    std::string(""));
}

/* --- parse_config_file (with temp file) --- */
TEST(parse_config_file_basic) {
    const char* tmp = "/tmp/test_opentenbase_ctl.ini";
    {
        std::ofstream f(tmp);
        f << "[instance]\n"
          << "name = test_instance\n"
          << "type = distributed\n"
          << "package = /tmp/opentenbase-5.21.8.tar.gz\n"
          << "\n"
          << "[gtm]\n"
          << "master = 10.0.0.1\n"
          << "slave = 10.0.0.2\n"
          << "\n"
          << "[coordinators]\n"
          << "master = 10.0.0.3,10.0.0.4\n"
          << "slave = 10.0.0.5,10.0.0.6\n"
          << "nodes-per-server = 2\n"
          << "\n"
          << "[datanodes]\n"
          << "master = 10.0.0.7\n"
          << "slave = 10.0.0.8\n"
          << "nodes-per-server = 1\n"
          << "\n"
          << "[server]\n"
          << "ssh-user = opentenbase\n"
          << "ssh-password = secret\n"
          << "ssh-port = 22\n"
          << "\n"
          << "[log]\n"
          << "level = debug\n";
    }
    ConfigFile cfg;
    EXPECT_EQ(parse_config_file(tmp, cfg), 0);
    EXPECT_EQ(cfg.instance.name,    std::string("test_instance"));
    EXPECT_EQ(cfg.instance.type,    std::string("distributed"));
    EXPECT_EQ(cfg.instance.package, std::string("/tmp/opentenbase-5.21.8.tar.gz"));
    EXPECT_EQ(cfg.gtm.master,       std::string("10.0.0.1"));
    EXPECT_EQ(cfg.gtm.slave,        std::string("10.0.0.2"));
    EXPECT_EQ(cfg.coordinators.master, std::string("10.0.0.3,10.0.0.4"));
    EXPECT_EQ(cfg.coordinators.nodes_per_server, std::string("2"));
    EXPECT_EQ(cfg.datanodes.master, std::string("10.0.0.7"));
    EXPECT_EQ(cfg.server.ssh_user,  std::string("opentenbase"));
    EXPECT_EQ(cfg.server.ssh_port,  std::string("22"));
    EXPECT_EQ(cfg.log.level,        std::string("debug"));
    std::remove(tmp);
}
TEST(parse_config_file_missing) {
    ConfigFile cfg;
    EXPECT_EQ(parse_config_file("/tmp/nonexistent_file_12345.ini", cfg), -1);
}
TEST(parse_config_file_comments_and_blanks) {
    const char* tmp = "/tmp/test_opentenbase_ctl_comments.ini";
    {
        std::ofstream f(tmp);
        f << "# This is a comment\n"
          << "; This is also a comment\n"
          << "\n"
          << "[instance]\n"
          << "name = myinst\n"
          << "# another comment\n"
          << "type = centralized\n";
    }
    ConfigFile cfg;
    EXPECT_EQ(parse_config_file(tmp, cfg), 0);
    EXPECT_EQ(cfg.instance.name, std::string("myinst"));
    EXPECT_EQ(cfg.instance.type, std::string("centralized"));
    std::remove(tmp);
}

/* ================================================================== */
/*  command.h tests                                                    */
/* ================================================================== */

/* --- parse_comma_separated_list --- */
TEST(parse_comma_list_basic) {
    auto v = parse_comma_separated_list("a,b,c");
    EXPECT_EQ(v.size(), (size_t)3);
    EXPECT_EQ(v[0], std::string("a"));
    EXPECT_EQ(v[1], std::string("b"));
    EXPECT_EQ(v[2], std::string("c"));
}
TEST(parse_comma_list_spaces) {
    auto v = parse_comma_separated_list(" hello , world ");
    EXPECT_EQ(v.size(), (size_t)2);
    EXPECT_EQ(v[0], std::string("hello"));
    EXPECT_EQ(v[1], std::string("world"));
}
TEST(parse_comma_list_single) {
    auto v = parse_comma_separated_list("single");
    EXPECT_EQ(v.size(), (size_t)1);
    EXPECT_EQ(v[0], std::string("single"));
}
TEST(parse_comma_list_null) {
    auto v = parse_comma_separated_list(nullptr);
    EXPECT_EQ(v.size(), (size_t)0);
}
TEST(parse_comma_list_empty) {
    auto v = parse_comma_separated_list("");
    EXPECT_EQ(v.size(), (size_t)0);
}

/* ================================================================== */
/*  file.h tests                                                       */
/* ================================================================== */

/* --- ltrim / rtrim / trim --- */
TEST(ltrim_basic) {
    EXPECT_EQ(ltrim("  hello"), std::string("hello"));
    EXPECT_EQ(ltrim("hello"),   std::string("hello"));
    EXPECT_EQ(ltrim(""),        std::string(""));
}
TEST(rtrim_basic) {
    EXPECT_EQ(rtrim("hello  "), std::string("hello"));
    EXPECT_EQ(rtrim("hello"),   std::string("hello"));
    EXPECT_EQ(rtrim(""),        std::string(""));
}
TEST(trim_basic) {
    EXPECT_EQ(trim("  hello  "), std::string("hello"));
    EXPECT_EQ(trim("hello"),     std::string("hello"));
    EXPECT_EQ(trim(""),          std::string(""));
    EXPECT_EQ(trim("\t\n"),      std::string(""));
}

/* --- parseConfigFile (key=value file) --- */
TEST(parseConfigFile_basic) {
    const char* tmp = "/tmp/test_kv_config.conf";
    {
        std::ofstream f(tmp);
        f << "shared_buffers = 256MB\n"
          << "max_connections = 100 # this is a comment\n"
          << "\n"
          << "work_mem = 4MB\n";
    }
    std::map<std::string, std::string> m;
    EXPECT_TRUE(parseConfigFile(tmp, m));
    EXPECT_EQ(m["shared_buffers"],  std::string("256MB"));
    EXPECT_EQ(m["max_connections"], std::string("100"));
    EXPECT_EQ(m["work_mem"],        std::string("4MB"));
    std::remove(tmp);
}
TEST(parseConfigFile_missing) {
    std::map<std::string, std::string> m;
    EXPECT_FALSE(parseConfigFile("/tmp/no_such_file_xyz.conf", m));
}

/* --- to_absolute_path --- */
TEST(to_absolute_path_existing) {
    std::string p = to_absolute_path("/tmp");
    EXPECT_FALSE(p.empty());
    EXPECT_EQ(p[0], '/');
}
TEST(to_absolute_path_nonexistent) {
    EXPECT_EQ(to_absolute_path("/nonexistent_path_xyz_123456"),
              std::string(""));
}

/* --- try_delete_file --- */
TEST(try_delete_file_existing) {
    const char* tmp = "/tmp/test_delete_me.txt";
    { std::ofstream f(tmp); f << "data"; }
    EXPECT_TRUE(try_delete_file(tmp));
}
TEST(try_delete_file_nonexistent) {
    EXPECT_FALSE(try_delete_file("/tmp/no_such_file_to_delete_xyz.txt"));
}

/* ================================================================== */
/*  Constants smoke tests                                              */
/* ================================================================== */
TEST(constants_sanity) {
    EXPECT_EQ(std::string(Constants::NODE_TYPE_GTM_MASTER), std::string("gtm_master"));
    EXPECT_EQ(std::string(Constants::NODE_TYPE_GTM_SLAVE),  std::string("gtm_slave"));
    EXPECT_EQ(std::string(Constants::NODE_TYPE_CN_MASTER),  std::string("cn_master"));
    EXPECT_EQ(std::string(Constants::NODE_TYPE_CN_SLAVE),   std::string("cn_slave"));
    EXPECT_EQ(std::string(Constants::NODE_TYPE_DN_MASTER),  std::string("dn_master"));
    EXPECT_EQ(std::string(Constants::NODE_TYPE_DN_SLAVE),   std::string("dn_slave"));
    EXPECT_EQ(std::string(Constants::INSTANCE_TYPE_CENTRALIZED), std::string("centralized"));
    EXPECT_EQ(std::string(Constants::INSTANCE_TYPE_DISTRIBUTED), std::string("distributed"));
    EXPECT_EQ(std::string(Constants::COMMAND_TYPE_GUC),     std::string("guc"));
    EXPECT_EQ(std::string(Constants::GUC_OP_SHOW),          std::string("show"));
    EXPECT_EQ(std::string(Constants::GUC_OP_DEL),           std::string("delete"));
    EXPECT_EQ(std::string(Constants::GUC_OP_CHANGE),        std::string("change"));
}

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
int main(int argc, char** argv) {
    // Suppress log output from the library during tests
    set_log_level(LOG_LEVEL_ERROR);

    for (auto& t : test_registry()) {
        g_tests_run++;
        try {
            t.fn();
            g_tests_passed++;
            std::cout << "  PASS  " << t.name << "\n";
        } catch (const std::exception& e) {
            g_tests_failed++;
            std::cerr << "  FAIL  " << t.name << " (" << e.what() << ")\n";
        }
    }

    std::cout << "\n=== Results: " << g_tests_passed << " passed, "
              << g_tests_failed << " failed, " << g_tests_run << " total ===\n";
    return g_tests_failed > 0 ? 1 : 0;
}
