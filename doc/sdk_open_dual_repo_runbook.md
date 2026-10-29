# sdk_open 双仓维护操作手册（主仓）

## 1. 目标

- 主仓 `czur-capeditor` 与 `sdk_open` 独立维护。
- 主仓通过 submodule 引用 `src/sdk_open`。
- 主仓固定到 `sdk_open` 的 tag/commit，不跟踪分支头。

## 2. 一次性迁移（目录 -> submodule）

前置要求：

- 主仓工作区干净。
- 已创建远端 `sdk_open` 仓并推送初始内容。

执行：

```bash
scripts/sdk/migrate_sdk_open_to_submodule.sh <sdk_open_repo_url> [branch]
```

说明：

- 脚本会先备份当前 `src/sdk_open` 到 `tmp_sdk_open_backup_<timestamp>`。
- 脚本会写入 `.gitmodules` 并创建 `src/sdk_open` submodule。

## 3. 日常升级（主仓跟随 sdk_open 新版本）

```bash
scripts/sdk/bump_sdk_open_submodule.sh <tag-or-commit> [remote]
```

示例：

```bash
scripts/sdk/bump_sdk_open_submodule.sh v0.1.0
```

执行后：

- `src/sdk_open` 会切到指定版本。
- 主仓自动 `git add src/sdk_open`，只需提交主仓指针更新。

## 4. CI 建议

- 主仓 CI 中加入：
  - `git submodule sync --recursive`
  - `git submodule update --init --recursive`
- 主仓构建矩阵建议：
  - `BUILD_SDK_OPEN=ON, BUILD_SDK_PRIVATE=OFF`
  - `BUILD_SDK_OPEN=ON, BUILD_SDK_PRIVATE=ON`

## 5. 风险与回滚

- 风险：submodule 指针升级后出现兼容问题。
- 回滚：将主仓中 `src/sdk_open` 指针回退到旧 commit 并提交即可。
