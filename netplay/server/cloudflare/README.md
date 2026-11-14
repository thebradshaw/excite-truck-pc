# Network Play relay on Cloudflare (free)

This is the default room server for Network Play. It only passes messages between players in the
same room; it stores nothing.

One-time setup (about 5 minutes):

1. Make a free account at https://dash.cloudflare.com/sign-up
2. Install Node.js LTS from https://nodejs.org
3. Double-click deploy-windows.bat (Windows), or in this folder run:

       npx wrangler login
       npx wrangler deploy

4. Wrangler prints the address, like `https://excite-netplay.<your-name>.workers.dev`.
   Players use it as `wss://excite-netplay.<your-name>.workers.dev` (the launcher's
   Network Play > Server field; the setup can bake it in as the default).

Test locally: `npx wrangler dev`, then `netplay_test ws://127.0.0.1:8787`.
