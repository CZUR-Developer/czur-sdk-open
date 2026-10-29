# CZUR SDK Auth JNA

This module is a Java wrapper for `libsdk_private_auth_c_api.so`.

## Runtime Requirements

- Build native target `sdk_private_auth_c_api`.
- Put `libsdk_private_auth_c_api.so` and its runtime dependencies on the server.
- By default, the native auth library uses the built-in production master secret. Set `CZUR_SDK_MASTER_SECRET` before the JVM starts only for tests, integration, or private deployments that intentionally override it.
- Point Java to the native library with either:
  - JVM property: `-Dczur.sdk.auth.library.path=/abs/path/libsdk_private_auth_c_api.so`
  - Environment variable: `CZUR_SDK_AUTH_LIBRARY_PATH=/abs/path/libsdk_private_auth_c_api.so`
  - Or ensure `sdk_private_auth_c_api` is resolvable through the OS library path.

## Example

```java
CzurSdkApiKeyIssuer issuer = CzurSdkApiKeyIssuer.createDefault();

GenerateApiKeyRequest request = new GenerateApiKeyRequest();
request.setTier("svip");
request.setLicenseMode("offline_api_key");
request.setHostAuthMode("activation_required");
// Optional: records the API key's own commercial authorization fact.
// Offline formal state is still decided by host activation.
request.setCommercialAuthorized(false);
request.setExpiresAt(0L);
request.setKeyId("key_project_123");
request.setDeviceScope(List.of(new DeviceGrant(4660, 22136)));

GenerateApiKeyResponse response = issuer.generate(request);
String apiKey = response.getApiKey();

GenerateAuthCodeRequest authCodeRequest =
        new GenerateAuthCodeRequest("customer-machine-code", "20261231", 13);
GenerateAuthCodeResponse authCodeResponse = issuer.generateAuthCode(authCodeRequest);
String authCode = authCodeResponse.getAuthCode();
```

The plain ApiKey should be returned to the caller once, then only a hash or digest should be stored.

Auth code generation uses the SDK private built-in license key. `expiresDate` must use `YYYYMMDD`.
`authType` must be a plugin-only or both grant type so the generated code can activate SDK offline host auth.


## Commercial Authorization Semantics

`commercialAuthorized` is embedded in the generated ApiKey payload and returned by parse/generate responses. Leave it `false` unless the server has confirmed the order, project, or package is commercially authorized.

For `offline_api_key`, host activation is authoritative: a key with `commercialAuthorized=false` still becomes `offline_unlocked` after a valid host auth code is applied. For `online_api_key`, the online auth service must return `commercialAuthorized=true` to enter `online_validated`; missing or false responses become `online_trial`, except CZUR-owned devices may locally activate VIP/SVIP but never SVIP+.
