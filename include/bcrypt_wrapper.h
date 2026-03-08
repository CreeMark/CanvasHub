#ifndef BCRYPT_WRAPPER_H
#define BCRYPT_WRAPPER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BCRYPT_HASHSIZE 64
#define BCRYPT_COST 6

int bcrypt_hashpw(const char *passwd, const char *salt, char *hash);
void bcrypt_gensalt(int workfactor, char *salt);
int bcrypt_newhash(const char *passwd, int workfactor, char *hash);
int bcrypt_checkpass(const char *passwd, const char *hash);

#ifdef __cplusplus
}
#endif

#endif
