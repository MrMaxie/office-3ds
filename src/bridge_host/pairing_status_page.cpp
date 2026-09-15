#include "office3ds/bridge_host/pairing_status_page.hpp"

#include <qrcodegen.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>

namespace office3ds::bridge_host {
namespace {

std::string escape_html(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
    case '&':
      escaped += "&amp;";
      break;
    case '<':
      escaped += "&lt;";
      break;
    case '>':
      escaped += "&gt;";
      break;
    case '"':
      escaped += "&quot;";
      break;
    case '\'':
      escaped += "&#39;";
      break;
    default:
      escaped += character;
      break;
    }
  }
  return escaped;
}

std::string safe_accent(std::string_view value) {
  if (value.size() != 7U || value.front() != '#') {
    return "#2A6F97";
  }
  for (const char character : value.substr(1)) {
    if (std::isxdigit(static_cast<unsigned char>(character)) == 0) {
      return "#2A6F97";
    }
  }
  return std::string(value);
}

std::string render_qr_svg(std::string_view payload) {
  const auto qr =
    qrcodegen::QrCode::encodeText(std::string(payload).c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
  constexpr int kBorder = 4;
  const auto size = qr.getSize() + kBorder * 2;
  std::ostringstream output;
  output << "<svg class=\"pairing-qr\" role=\"img\" aria-label=\"Temporary pairing QR code\" "
            "viewBox=\"0 0 "
         << size << ' ' << size
         << "\" xmlns=\"http://www.w3.org/2000/svg\" shape-rendering=\"crispEdges\">"
            "<rect width=\"100%\" height=\"100%\" fill=\"#fff\"/><path d=\"";
  for (int y = 0; y < qr.getSize(); ++y) {
    for (int x = 0; x < qr.getSize(); ++x) {
      if (qr.getModule(x, y)) {
        output << 'M' << x + kBorder << ',' << y + kBorder << "h1v1h-1z";
      }
    }
  }
  output << "\" fill=\"#17202a\"/></svg>";
  return output.str();
}

} // namespace

std::string render_pairing_status_page(std::string_view product_display_name,
                                       std::string_view advertised_host, std::uint16_t port,
                                       std::string_view accent_color,
                                       const bridge::PairingOffer &offer, bool available,
                                       bool credential_usable, bool claimed,
                                       std::chrono::seconds remaining, std::string_view reset_token,
                                       std::string_view claim_status) {
  const auto display_name = escape_html(product_display_name);
  const auto host = escape_html(advertised_host);
  const auto accent = safe_accent(accent_color);
  const auto pairing_code = escape_html(offer.pairing_code);
  const auto csrf_token = escape_html(reset_token);
  const auto status = escape_html(claim_status);
  const auto remaining_count = std::max<std::int64_t>(0, remaining.count());
  const auto remaining_minutes = remaining_count / 60;
  const auto remaining_seconds = remaining_count % 60;
  std::ostringstream countdown;
  countdown << std::setfill('0') << std::setw(2) << remaining_minutes << ':' << std::setw(2)
            << remaining_seconds;

  std::ostringstream page;
  page
    << R"(<!doctype html><html lang="en"><head><meta charset="utf-8">)"
    << R"(<meta name="viewport" content="width=device-width,initial-scale=1">)"
    << R"(<meta http-equiv="refresh" content="1">)"
    << R"(<meta http-equiv="Cache-Control" content="no-store,max-age=0">)"
    << R"(<meta name="referrer" content="no-referrer">)"
    << R"(<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; img-src data:; base-uri 'none'; form-action 'self'">)"
    << "<title>" << display_name << " - Pairing bridge</title><style>"
    << ":root{--accent:" << accent
    << R"(;--ink:#17202a;--paper:#333333;--panel:#fffdf8;--muted:#65727d}*{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;padding:24px;background:var(--paper);color:var(--ink);font:16px/1.45 system-ui,sans-serif}.card{width:min(100%,780px);border:3px solid var(--ink);background:var(--panel);box-shadow:10px 10px 0 var(--ink)}.accent{height:10px;background:var(--accent)}main{padding:30px}h1{margin:0 0 6px;font-size:clamp(1.6rem,5vw,2.35rem)}.lede{margin:0 0 26px;color:var(--muted)}.layout{display:grid;grid-template-columns:minmax(220px,340px) 1fr;gap:30px;align-items:center}.pairing-qr{display:block;width:100%;height:auto;background:#fff;border:8px solid #fff;outline:2px solid var(--ink)}.details{display:grid;gap:18px}.label{display:block;color:var(--muted);font-size:.76rem;font-weight:750;letter-spacing:.1em;text-transform:uppercase}.value{display:block;margin-top:3px;font-size:1.1rem;font-weight:750;overflow-wrap:anywhere}.code{color:var(--accent);font-size:2.1rem;letter-spacing:.16em}.timer{letter-spacing:.08em}.done{padding:26px 0 12px;font-size:1.15rem;font-weight:700}form{margin-top:24px}button{border:3px solid var(--ink);background:var(--accent);color:#fff;padding:12px 18px;font:inherit;font-weight:800;cursor:pointer;box-shadow:4px 4px 0 var(--ink)}button:hover{filter:brightness(.92)}button:active{transform:translate(2px,2px);box-shadow:2px 2px 0 var(--ink)}@media(max-width:620px){.layout{grid-template-columns:1fr}.pairing-qr{max-width:340px;margin:auto}})"
    << "</style></head><body><div class=\"card\"><div class=\"accent\"></div><main>"
    << "<h1>" << display_name << " pairing bridge</h1>";
  if (available) {
    page
      << R"(<p class="lede">Choose either method. Both pair the same one-time session.</p><div class="layout">)"
      << render_qr_svg(offer.qr_payload)
      << R"(<div class="details"><div><span class="label">Bridge address</span><span class="value">)"
      << host << ':' << port
      << R"(</span></div><div><span class="label">Pairing code</span><span class="value code">)"
      << pairing_code
      << R"(</span></div><div><span class="label">Time remaining</span><span class="value code timer">)"
      << countdown.str()
      << R"(</span></div><div><span class="label">Status</span><span class="value">)" << status
      << R"(</span></div></div></div>)";
  } else {
    if (claimed) {
      page
        << R"(<p class="done">Pairing completed. This single-use offer cannot be used again.</p>)";
    } else if (!credential_usable) {
      page
        << R"(<p class="done">The bearer credential expired. Issue a new credential and restart the bridge.</p>)";
    } else {
      page
        << R"(<p class="done">This pairing offer is unavailable. Create a new single-use offer.</p>)";
    }
  }
  if (credential_usable) {
    page << R"(<form method="post" action="/reset"><input type="hidden" name="csrf_token" value=")"
         << csrf_token << R"("><button type="submit">Generate new pairing code</button></form>)";
  }
  page << "</main></div></body></html>";
  return page.str();
}

} // namespace office3ds::bridge_host
