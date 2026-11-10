#ifndef AES_H
#define AES_H

#include <QString>

// AES-ECB（PKCS7 填充），对齐原易语言版内嵌 CryptoJS 的加密方式：
//   crypt.AES.encrypt(data, key, { mode: ECB })
// 密钥按 Latin1（每字符 1 字节）解析，长度为 4 字节整数倍（原版 12 字节密钥为非标准 AES）。
// 输出为 Base64 字符串（CryptoJS CipherParams 默认 toString 即 Base64）。

// 明文 -> Base64(密文)
QString aesEncryptBase64(const QString &plain, const QString &key);

// Base64(密文) -> 明文；输入非法或解密失败时返回空串
QString aesDecryptBase64(const QString &b64, const QString &key);

#endif // AES_H
