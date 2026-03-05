#include "unity.h"
#include "protocol.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {
}

void tearDown(void) {
}

void test_protocol_serialize_draw(void) {
    draw_msg_t msg;
    msg.tool_type = 1; // PEN
    msg.color = 0xFF0000;
    msg.width = 2.5;
    msg.point_count = 2;
    msg.points = malloc(sizeof(msg.points[0]) * 2);
    msg.points[0].x = 10.0;
    msg.points[0].y = 20.0;
    msg.points[1].x = 30.0;
    msg.points[1].y = 40.0;

    char *json = protocol_serialize_draw(&msg);
    TEST_ASSERT_NOT_NULL(json);
    
    // 简单检查包含关键字段
    TEST_ASSERT_NOT_NULL(strstr(json, "\"type\":\"draw\""));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"tool\":1"));
    TEST_ASSERT_NOT_NULL(strstr(json, "\"color\":16711680")); // 0xFF0000 = 16711680
    
    free(msg.points);
    free(json);
}

void test_protocol_deserialize_draw(void) {
    const char *json = "{\"type\":\"draw\",\"data\":{\"tool\":1,\"color\":16711680,\"width\":2.5,\"points\":[{\"x\":10,\"y\":20},{\"x\":30,\"y\":40}]}}";
    draw_msg_t msg;
    memset(&msg, 0, sizeof(msg));

    int ret = protocol_deserialize_draw(json, &msg);
    TEST_ASSERT_EQUAL(0, ret);
    TEST_ASSERT_EQUAL(1, msg.tool_type);
    TEST_ASSERT_EQUAL_HEX32(0xFF0000, msg.color);
    TEST_ASSERT_EQUAL_DOUBLE(2.5, msg.width);
    TEST_ASSERT_EQUAL(2, msg.point_count);
    TEST_ASSERT_EQUAL_DOUBLE(10.0, msg.points[0].x);
    TEST_ASSERT_EQUAL_DOUBLE(20.0, msg.points[0].y);
    TEST_ASSERT_EQUAL_DOUBLE(30.0, msg.points[1].x);
    TEST_ASSERT_EQUAL_DOUBLE(40.0, msg.points[1].y);

    protocol_free_draw_msg(&msg);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_protocol_serialize_draw);
    RUN_TEST(test_protocol_deserialize_draw);
    return UNITY_END();
}
