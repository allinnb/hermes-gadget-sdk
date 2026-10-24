# Server Deployment Guide — Hermes Agent + Gadget Plugin

> 运行 Hermes Agent 服务端，对接 M5Stack StopWatch（或任何 Hermes Gadget 设备），实现语音对话。
> 实测环境：腾讯云 2C2G（实际 3.6G 内存），Ubuntu/OpenCloudOS，Hermes v0.22.5。

---

## 1. 安装 Hermes Agent

```bash
# 经 gh-proxy 加速（国内直连 github 慢）
export HERMES_REPO_URL="https://gh-proxy.com/https://github.com/NousResearch/hermes-agent.git"
curl -fsSL https://raw.githubusercontent.com/NousResearch/hermes-agent/main/install.sh -o /tmp/hermes-install.sh
bash /tmp/hermes-install.sh
# 二进制在 ~/.local/bin/hermes
hermes --version
```

> 注意：installer 内置 1800 秒超时，慢网环境可能在 `uv sync` 阶段失败。
> 失败后手动补装依赖再重跑 installer（见第 7 节排错）。

## 2. 配置 LLM（OpenAI 兼容接口）

```bash
hermes config set model.provider custom
hermes config set model.base_url https://YOUR_LLM_ENDPOINT/v1
hermes config set model.default YOUR_MODEL_NAME
hermes config set model.api_key  # 交互式输入，不进 shell 历史
```

验证：

```bash
curl -s https://YOUR_LLM_ENDPOINT/v1/chat/completions \
  -H "Authorization: Bearer $KEY" \
  -H "Content-Type: application/json" \
  -d '{"model":"YOUR_MODEL_NAME","messages":[{"role":"user","content":"hi"}]}'
```

## 3. 安装 Gadget 插件

```bash
# --yes-deps 是关键，否则依赖装不全
hermes plugins install https://github.com/Adolanium/hermes-gadget-sdk/tree/main/plugin --enable --yes-deps
hermes config set platforms.gadget.enabled true
```

## 4. 配置中文 TTS（Piper）

```bash
# 1. 装 piper-tts（注意装进网关的 venv，不是 agent 的）
export UV_INDEX_URL="https://pypi.tuna.tsinghua.edu.cn/simple"  # 国内镜像
/root/.hermes/tools/uv-*/uv pip install --python \
  /root/.hermes/installs/*/environments/*/venv/bin/python piper-tts

# 2. 下中文语音包（HuggingFace 国内直连可能被墙，先在本地下好再传）
# https://huggingface.co/rhasspy/piper-voices/resolve/main/zh/zh_CN/huayan/medium/zh_CN-huayan-medium.onnx
# https://huggingface.co/rhasspy/piper-voices/resolve/main/zh/zh_CN/huayan/medium/zh_CN-huayan-medium.onnx.json
mkdir -p ~/.hermes/cache/piper-voices/
cp zh_CN-huayan-medium.onnx* ~/.hermes/cache/piper-voices/

# 3. 配置
hermes config set tts.provider piper
hermes config set tts.piper.voice zh_CN-huayan-medium
```

## 5. 配置 STT（Whisper 本地）

```bash
# 装进网关 venv（同第 4 节路径）
uv pip install --python /root/.hermes/installs/*/environments/*/venv/bin/python openai-whisper

# config.yaml 里加（stt: 节下）：
#   language: "zh"   # 锁死中文，跳过自动检测
```

首次运行会自动下载 `base` 模型（~139MB）到 `~/.cache/whisper/`。

## 6. 启动网关（systemd）

Hermes 自带的 `gateway install` 依赖 D-Bus，服务器环境通常走不通。手写 unit：

```ini
# /etc/systemd/system/hermes-gateway.service
[Unit]
Description=Hermes Agent Gateway
After=network.target

[Service]
Type=simple
User=root
ExecStart=/root/.local/bin/hermes gateway run
Restart=always
Environment=GADGET_ALLOWED_USERS=<device-id-1>,<device-id-2>

[Install]
WantedBy=multi-user.target
```

```bash
systemctl daemon-reload
systemctl enable --now hermes-gateway
# 8765 端口监听 0.0.0.0，安全组放行（或按需限 IP）
```

> `GADGET_ALLOWED_USERS` 是逗号分隔的设备 ID 白名单。设备配对后把新 ID 加进来并 `daemon-reload` + `restart`。

## 7. 配对设备

表端串口执行：

```
set server ws://YOUR_SERVER_IP:8765/gadget
```

表屏显示配对码后，服务端批准：

```bash
hermes gadget pair --yes
```

## 8. 语音优化（可选但推荐）

`~/.hermes/config.yaml` 的 `agent:` 节下加：

```yaml
agent:
  system_prompt: "你是用户的手表语音助手。用简短口语中文回复，每次不超过3句话。直接说重点，不要铺垫。如果用户的话含糊不清、像是转写错误或无意义，不要编造回答，直接说：没听清，请再说一遍。"
  max_tokens: 300
```

改完重启网关。

## 9. 可选：天气 Skill

`~/.hermes/skills/productivity/weather/SKILL.md`（调 Open-Meteo，免 key）：

```markdown
---
name: weather
description: "Get real-time weather via Open-Meteo (no API key)."
---
# Weather
...
```

详见本仓库 `skills/` 目录示例。

## 10. 排错

| 症状 | 原因 | 解法 |
|------|------|------|
| `Unauthorized user` | 设备 ID 不在白名单 | 加进 `GADGET_ALLOWED_USERS` 并重启 |
| `Piper provider selected but 'piper-tts' package not installed` | piper 装错 venv | 装进网关的 venv（第 4 节路径） |
| `Piper voice download failed` | 语音包缺失 + HF 被墙 | 手动下载放 `~/.hermes/cache/piper-voices/` |
| `Local STT command failed` | whisper 装错 venv | 同 piper，装进网关 venv |
| `APIConnectionError` 调 LLM | 网关到 LLM 端点网络不通 | curl 验证连通性 |
| 表端 `thinking` 卡死 | 网关重启掐断会话 | 表端重连（`set server` 重跑一遍） |
| 磁盘满 (`No space left`) | `~/.hermes/cache` 膨胀 | 清理后重下语音包（注意别删 `piper-voices/`） |

---

*指南版本 v1，2026-10-24。实测于 Hermes v0.22.5 + hermes-gadget-sdk plugin。*
