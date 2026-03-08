#include <stdio.h>
#include <string.h>
#include "bcrypt_wrapper.h"

int main() {
    printf("=== bcrypt 密码哈希测试 ===\n\n");
    
    const char *password = "MySecurePassword123!";
    const char *wrong_password = "WrongPassword";
    
    printf("测试密码: %s\n", password);
    printf("成本系数: %d\n\n", BCRYPT_COST);
    
    char hash1[BCRYPT_HASHSIZE];
    char hash2[BCRYPT_HASHSIZE];
    
    printf("1. 生成第一个哈希...\n");
    if (bcrypt_newhash(password, BCRYPT_COST, hash1) != 0) {
        printf("错误: 哈希生成失败\n");
        return 1;
    }
    printf("哈希1: %s\n", hash1);
    printf("长度: %zu 字符\n\n", strlen(hash1));
    
    printf("2. 生成第二个哈希（相同密码，不同盐值）...\n");
    if (bcrypt_newhash(password, BCRYPT_COST, hash2) != 0) {
        printf("错误: 哈希生成失败\n");
        return 1;
    }
    printf("哈希2: %s\n", hash2);
    printf("长度: %zu 字符\n\n", strlen(hash2));
    
    printf("3. 验证两个哈希不同（因为盐值不同）...\n");
    if (strcmp(hash1, hash2) != 0) {
        printf("✓ 通过: 两个哈希不同\n\n");
    } else {
        printf("✗ 失败: 两个哈希相同\n\n");
        return 1;
    }
    
    printf("4. 验证正确密码...\n");
    if (bcrypt_checkpass(password, hash1) == 0) {
        printf("✓ 通过: 正确密码验证成功\n\n");
    } else {
        printf("✗ 失败: 正确密码验证失败\n\n");
        return 1;
    }
    
    printf("5. 验证错误密码...\n");
    if (bcrypt_checkpass(wrong_password, hash1) != 0) {
        printf("✓ 通过: 错误密码验证失败（预期行为）\n\n");
    } else {
        printf("✗ 失败: 错误密码验证成功（不应该）\n\n");
        return 1;
    }
    
    printf("6. 验证密码对第二个哈希...\n");
    if (bcrypt_checkpass(password, hash2) == 0) {
        printf("✓ 通过: 密码对第二个哈希验证成功\n\n");
    } else {
        printf("✗ 失败: 密码对第二个哈希验证失败\n\n");
        return 1;
    }
    
    printf("=== 所有测试通过 ===\n");
    printf("\n安全特性验证:\n");
    printf("✓ 密码已加盐哈希存储\n");
    printf("✓ 每次生成不同的哈希值（随机盐值）\n");
    printf("✓ 正确密码可以验证通过\n");
    printf("✓ 错误密码验证失败\n");
    printf("✓ 成本系数: %d (2^%d = %d 轮加密)\n", 
           BCRYPT_COST, BCRYPT_COST, 1 << BCRYPT_COST);
    
    return 0;
}
