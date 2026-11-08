#pragma once
// Copie para Base_config_local.h, configure sua senha e altere 0 -> 1.
#define SMARTCOLLAR_CONFIG_PRONTA 0
#if !SMARTCOLLAR_CONFIG_PRONTA
#error "Configure sua senha em Base_config_local.h e altere SMARTCOLLAR_CONFIG_PRONTA para 1"
#endif
const char *AP_NOME = "SmartCollar-Base";
const char *AP_SENHA = "SUBSTITUIR_POR_SENHA_UNICA_FORTE";
