@echo off
rem ============================================================
rem  Starts the Telegram relay for the iPod bot.
rem  The bot only works while this window stays open!
rem  (The relay speaks HTTPS to Telegram on the PC's behalf,
rem   the ESP32 only speaks plain LAN HTTP - see tg_relay.ps1)
rem ============================================================
title iPod Telegram Relay - leave this window open
powershell -ExecutionPolicy Bypass -File "%~dp0tg_relay.ps1"
