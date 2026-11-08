#!/usr/bin/env python3
"""Checagem de estrutura, isolamento de credenciais e compatibilidade LoRa."""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]
roles = {
    'bovinos': ('SmartCollar_Research_v313', 'SmartCollar_config_local'),
    'base-lora': ('SmartCollar_Base_v14', 'Base_config_local'),
    'chihuahua': ('ChihuahuaLab_v15', 'Chihuahua_config_local'),
}
for role, (sketch, conf) in roles.items():
    folder = ROOT / 'firmware' / role / sketch
    assert (folder / (sketch + '.ino')).is_file(), (role, 'sketch ausente')
    assert (folder / (conf + '.example.h')).is_file(), (role, 'modelo ausente')
    assert not (folder / (conf + '.h')).exists(), (role, 'configuracao privada presente!')
    s = (folder / (sketch + '.ino')).read_text(encoding='utf8')
    assert f'#include "{conf}.h"' in s, (role, 'inclusao de configuracao ausente')
    assert not re.search(r'(?:AP_SENHA|WIFI_AP_SENHA|WIFI_CASA_SENHA)\s*=\s*"', s), (role, 'senha embutida em .ino')
p1 = ROOT / 'firmware/bovinos/SmartCollar_Research_v313/SmartCollar_lora.h'
p2 = ROOT / 'firmware/base-lora/SmartCollar_Base_v14/SmartCollar_lora.h'
assert p1.read_bytes() == p2.read_bytes(), 'Cabecalhos LoRa divergem'
print('Estrutura, isolamento de credenciais e protocolo LoRa: OK')
