# LiQuiDNicK accounts: keep logged in, verification, signed-in PATCH GEN

Adds to the existing PHP accounts at `app.liquidnick.com` (`login.php`, `register.php`, `account.php`, `logout.php`).
No code yet: this is the plan.

Mockups (7 screens, private): https://claude.ai/artifact/EjH4yoaBupos7RLXMEJnUt — sign them off before any work.

## What's already there (found 2026-11-05)

From `Desktop\liquidnick_site_work\members-addon` and the copy of `private/lib.php` in `ACID_BEDROCK_legal_update.zip`:
- One member app on app.liquidnick.com (ACID BEDROCK), PHP + MySQL, config in `private/config.php`.
- `users` has `email`, `pass_hash`, `status`, `session_token`. Paid access = `status` in active / trialing / lifetime / comp.
- **Stripe is already wired**: one price (`price_label` "$9 / month"), `stripe_call()` over cURL, `stripe_verify_signature()`,
  so there is already a checkout and a webhook on the server.
- Acid Bedrock is paid-only today (the app file is only served to paid users).
- **One login per account**: a new sign-in signs out every other device (`session_token`). That has to change for
  "keep me logged in" on several devices and for phone QR.
- Login throttling exists (`attempts` table, `throttle_ok` / `throttle_hit`).
- PATCH GEN access is an email list (`patchgen_emails` in config), checked in `members-addon/patchgen.php`.
- Not on this machine: `account.php`, checkout, webhook, `install.php` (schema), Acid Bedrock's app file. Needed before
  billing code is written, so the new code extends them instead of clashing.

## 1. Keep me logged in

- Checkbox on log in and on create account: "Keep me logged in on this device", 30 days. Off = session cookie only.
- Remember-me token as `selector:validator`. Store `selector`, `sha256(validator)`, user id, expiry, device label, last IP / city
  in `auth_tokens`. Cookie: `HttpOnly; Secure; SameSite=Lax; Path=/`, 30-day `Max-Age`.
- New validator on every use (rotation). If a selector arrives with the wrong validator, treat it as theft: delete all of
  that user's tokens and email them.
- Sliding window: each visit pushes expiry out, capped at 30 days from the last full sign-in with verification.
- Being remembered is not enough for sensitive actions: changing email or password, removing a verification method,
  deleting the account and exporting data all ask for the password (or a passkey) again.
- `session_regenerate_id(true)` on every sign-in. Log out deletes the token row as well as the cookie.
- Security page lists every remembered device with "Log out" and "Log out everywhere else".

## 2. Verification layer

Asked after the password, only on a device that isn't trusted yet (or after 30 days). "Trust this device for 30 days" ticks
by default. Email is verified with a code at sign-up, so email code is always there as a fallback.

| Method | How | Notes |
|---|---|---|
| **Live mobile QR** (default once set up) | Desktop shows a QR for `app.liquidnick.com/q/<token>`. Phone, already signed in and trusted, opens it, sees device / place / destination, taps the matching number, approves. Desktop waits (poll every 2 s, or SSE) and is signed in. | Token: 128-bit random, single use, 2 min life, new one every 2 min. Bound to the desktop's browser by a cookie nonce, so a photo of the QR on another machine is useless. Number match (pick 1 of 3) stops someone luring the user into approving the attacker's login. "Not me" blocks the request, logs it and asks for a password change. |
| Authenticator app (TOTP) | 6 digits, 30 s, RFC 6238. Setup shows a QR + key, confirmed with one code. | Secret encrypted at rest. Accept ±1 step. Remember the last used step so a code can't be used twice. |
| Passkey (WebAuthn) | Fingerprint / face / PIN / security key. Can also be the whole sign-in (button on log in). | Use a maintained PHP WebAuthn library, not our own. |
| Email code | 6 digits plus a one-tap link, 10 min. | Resend limit, 45 s cooldown. |
| Backup codes | 10 codes made when the first method is set up. | Stored hashed, each works once, "make new codes" voids the old ones. |

Rate limits on every step: 5 wrong codes then a 15 min lock for that account plus the IP. Every sign-in, block and change goes
in a per-user activity log shown on the security page. New-device sign-ins send an email.

**The live QR look.** The QR uses the NoiZeeToyZ colours: cyan dots on near-black, magenta corner squares, a round "NZT" badge
in the middle, a magenta scan line, a colour wave across the dots and a countdown bar. To keep it
scannable:
- Animate only colour and brightness, never whether a dot is on or off. The three corner squares and a 4-dot blank border
  around the code stay still, with high contrast.
- Use error correction level H (30%) so the centre badge can cover up to ~15% of the code.
- Light-on-dark codes don't scan with some older phone cameras: test on iPhone, Pixel, Samsung and one old Android, and keep
  the "show a plain black-and-white code" link.
- Turn the animation off when the system asks for reduced motion.
- The mockup also has dots that breathe (shrink and grow), rings rippling out from the badge, two rings spinning round
  it and sparks running round the frame. On the real code, keep dots at 70% size or more and keep everything that spins
  inside the cleared centre. Test each effect with real phones before keeping it.

The phone has to be signed in on its own first (password + email code) before it can approve QR requests; the first
approval asks it to "Use this phone for sign-in approvals".

## 3. Signed-in state in PATCH GEN

- `app.liquidnick.com/patchgen/` already says "invited members, log in". With a session, show the app; without one,
  keep today's preview card. `login.php?next=/patchgen/` already exists; check `next` is a local path only.
- Header: account button (initial, email, "● SIGNED IN · MEMBER"), menu with "kept logged in until …", My account,
  Security & devices, Acid Bedrock, Log out.
- **My patches**: save genre + seed + module list per patch (no library, no paths), as in the PATCH GEN plan's
  accounts section. The library itself stays on the device (IndexedDB) as planned.
- Session ends mid-use: a small "you've been logged out, log in again" bar; the rolled patch stays in the page.
- `liquidnick.com` home: the "● Signed in" link should come from the session (a small `/me` JSON endpoint on `app.`,
  credentials included, CORS allowed only for `https://liquidnick.com`), not be fixed.

## 4. "What you get" copy

- Create account: four cards (PATCH GEN, Acid Bedrock, saved & synced, first look) plus "what we keep / what we don't".
- Log in: a short "one account, every tool" side panel.
- liquidnick.com: a new FREE ACCOUNT section with the same four points and two buttons; turns into a "Signed in as …"
  strip when logged in.
- To confirm before shipping: what members really get early (the "first look" card), whether there's a paid tier
  (the site links Stripe), and whether the "new tools" email opt-in is wanted.

## 5. Plans and billing

Free is meant to show the tools work and run out quickly; Member is the plan most people should land on; Pro is for heavy
users. Prices set 2026-11-05.

| | Free · $0 | Member · $5/mo ($50/yr) | Pro · $12/mo ($120/yr) |
|---|---|---|---|
| PATCH GEN rolls / month | 10 | 100 | Unlimited (fair use 1,000) |
| Patches per roll | 1 | up to 5 | up to 20, as one `.zip` |
| PATCH GEN styles | 8 of 23 (House, Techno, Acid, Trance, Hip-Hop, Lo-Fi, Drum & Bass, Ambient) | all 23 | all 23, new styles first |
| Control | style only | + tempo, key | + scale, lock parts and re-roll the rest |
| Acid Bedrock tracks / month | 5 | 60 | Unlimited (fair use) |
| Acid Bedrock styles | 1/3 | all | all, new first |
| Acid Bedrock exports | MIDI | MIDI, prompts, scripts | everything + batch export |
| Saved items | last 3 | 50 | unlimited |
| Share links | — | yes | yes |
| Extras | — | — | early SynTHBloK modules and betas, supporter badge |

Free patches are not made worse: same generator, same tone. The limits are count, styles, control and exports.

**Upgrade nudges (honest, no fake timers)**
- Locked styles stay visible with a lock; clicking one opens "Unlock all 16 styles".
- Usage meter next to ROLL. At 2 left: "Running low. Member gets 100 a month."
- After a free roll: "Members get 5 versions of this patch to pick from."
- At zero: ROLL becomes "Out until <reset date> · Upgrade", with the real reset date.
- 7-day Pro trial, once per account. Yearly shown as "2 months free".

**Enforcement.** The browser builds the patch, so limits must be checked on the server before it does:
1. Page calls `POST /api/roll` with style, count and options.
2. Server checks plan, style allowed, `count <= plan max`, and the month's usage (row in `usage` keyed by user + month,
   `UPDATE … SET rolls = rolls + n WHERE rolls + n <= limit`, so two tabs can't go over).
3. Server returns a signed roll ticket (user, style, seeds, expiry 5 min, HMAC). The page builds the patch only from the
   seeds in the ticket, so a free user can't make more patches than the server issued.
4. Saves and share links send the ticket back; the server checks the signature.
Locked styles' genre data is not sent to free accounts at all.
Same scheme for Acid Bedrock (`POST /api/track`), and its exports are checked server-side by plan.

**Stripe.**
- Stripe Checkout for sign-up and upgrades, Stripe Customer Portal for card changes, plan switches and cancelling.
  No card forms on our site.
- Webhooks (`checkout.session.completed`, `customer.subscription.updated` / `deleted`, `invoice.payment_failed`) set
  `users.plan` and `plan_until`. Check the webhook signature, and handle each event id once.
- Downgrade at the end of the paid period. Past the free cap, saved items stay readable but no new saves until under the cap.
- Failed payment: 7 days' grace with a banner, then Free.
- Prices live in Stripe; the site reads them from one config file, so changing a price is a one-line change.

## 6. Order of work

| # | Step | Effort |
|---|---|---|
| 1 | Keep me logged in + devices list + log out everywhere | 2–3 days |
| 2 | Email verification at sign-up + email code step | 1–2 days |
| 3 | Live mobile QR (token table, phone approve page, number match, polling) | 3–4 days |
| 4 | TOTP + backup codes | 2 days |
| 5 | Passkeys | 2–3 days |
| 6 | PATCH GEN signed-in header, My patches, `/me` for the home page | 2–3 days |
| 7 | Copy: register / login panels, site section | 1 day |
| 8 | Plans: `plan` on users, `usage` table, `/api/roll` tickets, limits in PATCH GEN and Acid Bedrock | 3–4 days |
| 9 | Stripe Checkout + portal + webhooks, pricing page, upgrade pop-ups, trial | 3–4 days |
