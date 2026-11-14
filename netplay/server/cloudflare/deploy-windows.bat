@echo off
rem Puts the Excite PC Network Play room server on your free Cloudflare account.
rem Needs Node.js LTS from https://nodejs.org
cd /d "%~dp0"
echo A browser window opens: sign in to Cloudflare and allow Wrangler.
call npx --yes wrangler@3 login
call npx --yes wrangler@3 deploy
echo.
echo Copy the https://excite-netplay....workers.dev address above and send it to Claude.
pause
