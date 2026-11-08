#pragma once
// Copie para Chihuahua_config_local.h, configure sua senha e altere 0 -> 1.
#define SMARTCOLLAR_CONFIG_PRONTA 0
#if !SMARTCOLLAR_CONFIG_PRONTA
#error "Configure sua senha em Chihuahua_config_local.h e altere SMARTCOLLAR_CONFIG_PRONTA para 1"
#endif
const char *WIFI_AP_NOME = "ChihuahuaLab-001";
const char *WIFI_AP_SENHA = "SUBSTITUIR_POR_SENHA_UNICA_FORTE";
// Opcional: Wi-Fi da sua casa. Deixe vazio para usar apenas a rede local do dispositivo.
const char *WIFI_CASA_NOME = "";
const char *WIFI_CASA_SENHA = "";
