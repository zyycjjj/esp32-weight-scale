from os import environ
from os.path import join, dirname, exists

Import("env")


def _load_env_file(path):
    out = {}
    with open(path, "r", encoding="utf-8") as f:
        for raw in f.read().splitlines():
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#"):
                continue
            if "=" not in line:
                continue
            k, v = line.split("=", 1)
            k = k.strip()
            v = v.strip()
            if not k:
                continue
            out[k] = v
    return out


def _escape_c_string(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


project_dir = env["PROJECT_DIR"]
env_file = join(project_dir, ".aiw_secrets.env")

values = {}
if exists(env_file):
    values.update(_load_env_file(env_file))
values.update(environ)

keys = [
    "AIW_WIFI_SSID",
    "AIW_WIFI_PASSWORD",
    "AIW_BACKEND_BASE_URL",
    "AIW_DEVICE_ID",
    "AIW_DEVICE_NAME",
    "AIW_HX711_DOUT_PIN",
    "AIW_HX711_SCK_PIN",
]

defines = []
for k in keys:
    v = values.get(k)
    if v is None:
        continue
    if v == "":
        continue
    if k.endswith("_PIN") and v.isdigit():
      defines.append((k, v))
    else:
      defines.append((k, f"\\\"{_escape_c_string(v)}\\\""))

if defines:
    env.Append(CPPDEFINES=defines)
