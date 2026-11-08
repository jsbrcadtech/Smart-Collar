#pragma once
// Copie este arquivo para SmartCollar_config_local.h, edite sua senha e altere 0 -> 1.
// O .gitignore impede o commit do arquivo local.
#define SMARTCOLLAR_CONFIG_PRONTA 0
#if !SMARTCOLLAR_CONFIG_PRONTA
#error "Configure sua senha em SmartCollar_config_local.h e altere SMARTCOLLAR_CONFIG_PRONTA para 1"
#endif
const char *AP_NOME = "SmartCollar-Lab";
const char *AP_SENHA = "SUBSTITUIR_POR_SENHA_UNICA_FORTE";
const char *ANIMAL_ID = "ANIMAL001"; // Identificador nao pessoal, ate 10 caracteres no LoRa
