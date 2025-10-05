#include "WebUi.h"

#include "AppConfig.h"

namespace {

String htmlEscape(const String& text)
{
  String out;
  out.reserve(text.length());
  for (size_t i = 0; i < text.length(); ++i) {
    const char c = text[i];
    switch (c) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += c; break;
    }
  }
  return out;
}

String artnetInputLabel(uint8_t value)
{
  switch (value) {
    case 1: return F("Wi-Fi");
    case 2: return F("Automático");
    default: return F("Ethernet");
  }
}

}  // namespace

String renderConfigPage(const AppConfig& config,
                        const WebUiRuntime& runtime,
                        const String& message)
{
  const bool usingDhcp = config.useDhcp;
  const bool wifiEnabled = config.wifiEnabled;
  const bool wifiApMode = config.wifiApMode;

  const String staticIpStr   = ipToString(config.staticIp);
  const String staticGwStr   = ipToString(config.staticGateway);
  const String staticMaskStr = ipToString(config.staticSubnet);
  const String staticDns1Str = ipToString(config.staticDns1);
  const String staticDns2Str = ipToString(config.staticDns2);

  const String wifiStaSsidEsc = htmlEscape(config.wifiStaSsid);
  const String wifiStaPassEsc = htmlEscape(config.wifiStaPassword);
  const String wifiApSsidEsc  = htmlEscape(config.wifiApSsid);
  const String wifiApPassEsc  = htmlEscape(config.wifiApPassword);

  String wifiStatusText;
  if (!wifiEnabled) {
    wifiStatusText = F("Deshabilitado");
  } else if (wifiApMode) {
    wifiStatusText = runtime.wifiApRunning ? F("AP activo") : F("Inicializando AP");
  } else {
    if (runtime.wifiStaConnected) {
      wifiStatusText = runtime.wifiStaHasIp ? F("Conectado") : F("Sin IP (conectando)");
    } else {
      wifiStatusText = F("Buscando red…");
    }
  }

  String wifiModeLabel = wifiApMode ? F("Punto de acceso") : F("Cliente");
  String wifiSsidLabel = wifiApMode ? config.wifiApSsid :
                         (runtime.wifiStaSsidCurrent.length() ? runtime.wifiStaSsidCurrent : config.wifiStaSsid);
  wifiSsidLabel.trim();
  if (wifiSsidLabel.length() == 0) {
    wifiSsidLabel = F("(no configurado)");
  }
  const String wifiSsidStatus = htmlEscape(wifiSsidLabel);

  const String wifiClientsStr = (wifiEnabled && wifiApMode) ? String(runtime.wifiClientCount) : String('-');

  String artnetActiveLabel = F("Sin enlace");
  if (runtime.artnetIp != IPAddress((uint32_t)0)) {
    if (runtime.artnetIp == runtime.ethLocalIp) {
      artnetActiveLabel = F("Ethernet");
    } else if (runtime.artnetIp == runtime.wifiStaIp ||
               runtime.artnetIp == runtime.wifiApIp ||
               runtime.artnetIp == runtime.wifiLocalIp ||
               runtime.artnetIp == runtime.wifiSoftApIp) {
      artnetActiveLabel = F("Wi-Fi");
    } else {
      artnetActiveLabel = F("Desconocido");
    }
  }

  const String artnetIpStr = (runtime.artnetIp != IPAddress((uint32_t)0)) ? runtime.artnetIp.toString() : String('-');
  IPAddress wifiCurrentIp = runtime.wifiStaHasIp ? runtime.wifiStaIp :
                            (runtime.wifiApRunning ? runtime.wifiApIp : IPAddress((uint32_t)0));
  const String wifiIpStr = (wifiCurrentIp != IPAddress((uint32_t)0)) ? wifiCurrentIp.toString() : String('-');

  const String fallbackLabel = config.fallbackToStatic ?
                               F("Aplicar IP fija configurada") :
                               F("Mantener sin IP");

  String html;
  html.reserve(9000);
  html += F("<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>PixelEtherLED - Configuración</title>");
  html += F("<style>:root{color-scheme:dark;}body{font-family:'Segoe UI',Helvetica,Arial,sans-serif;background:#080b14;color:#f0f0f0;margin:0;}\n");
  html += F("header{background:linear-gradient(135deg,#111a30,#0b4bd8);padding:1.75rem;text-align:center;box-shadow:0 8px 20px rgba(0,0,0,0.55);}\n");
  html += F("header h1{margin:0;font-size:2rem;font-weight:700;}header p{margin:0.35rem 0 0;color:#d0dcff;font-size:1rem;}\n");
  html += F(".menu{display:flex;flex-wrap:wrap;justify-content:center;gap:0.75rem;padding:1rem 1.5rem;background:#0d1424;box-shadow:0 6px 18px rgba(0,0,0,0.45);}\n");
  html += F(".menu-item{display:flex;align-items:center;gap:0.75rem;padding:0.75rem 1.25rem;border-radius:12px;font-weight:600;text-decoration:none;color:#fff;box-shadow:0 6px 12px rgba(0,0,0,0.35);transition:transform 0.2s ease,box-shadow 0.2s ease;}\n");
  html += F(".menu-item:hover{transform:translateY(-2px);box-shadow:0 10px 24px rgba(0,0,0,0.45);}\n");
  html += F(".menu-item .icon{font-size:1.4rem;}\n");
  html += F(".menu-item.ethernet{background:linear-gradient(135deg,#1455ff,#0b2e99);}\n");
  html += F(".menu-item.artnet{background:linear-gradient(135deg,#9c27b0,#5e1673);}\n");
  html += F(".menu-item.wifi{background:linear-gradient(135deg,#00c6ff,#0072ff);}\n");
  html += F(".menu-item.leds{background:linear-gradient(135deg,#ff8a00,#e52e71);}\n");
  html += F(".menu-item.system{background:linear-gradient(135deg,#2bc0e4,#1b6fa8);}\n");
  html += F(".menu-item.preview{background:linear-gradient(135deg,#4caf50,#2e7d32);}\n");
  html += F(".content{padding:1.5rem;max-width:920px;margin:0 auto;}\n");
  html += F("form{margin:0;}\n");
  html += F(".panel{background:#101728;border-radius:16px;padding:1.5rem;margin-bottom:1.5rem;box-shadow:0 16px 32px rgba(0,0,0,0.45);}\n");
  html += F(".panel-title{display:flex;align-items:center;gap:0.6rem;margin:0 0 1.25rem;font-size:1.35rem;font-weight:700;color:#f5f7ff;}\n");
  html += F(".panel-title .badge{font-size:1.5rem;}\n");
  html += F("label{display:block;margin-bottom:0.4rem;font-weight:600;}\n");
  html += F("input[type=number],input[type=text],input[type=password],select{width:100%;padding:0.65rem 0.75rem;border-radius:10px;border:1px solid #23314d;background:#0b1322;color:#f0f0f0;margin-bottom:1.1rem;box-sizing:border-box;}\n");
  html += F("input[type=number]:focus,input[type=text]:focus,input[type=password]:focus,select:focus{outline:none;border-color:#3f7bff;box-shadow:0 0 0 2px rgba(63,123,255,0.25);}\n");
  html += F(".password-field{position:relative;display:flex;align-items:center;}\n");
  html += F(".password-field input{flex:1;padding-right:2.5rem;}\n");
  html += F(".password-field .toggle-password{position:absolute;right:0.6rem;top:50%;transform:translateY(-50%);background:transparent;border:none;color:#9bb3ff;cursor:pointer;font-size:1.1rem;padding:0.25rem;line-height:1;}\n");
  html += F(".password-field .toggle-password:hover,.password-field .toggle-password:focus{color:#ffffff;outline:none;}\n");
  html += F(".dual{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:1rem;}\n");
  html += F(".wifi-group{margin-bottom:1.25rem;padding:1.1rem;border-radius:12px;background:rgba(0,114,255,0.08);border:1px solid rgba(76,142,255,0.35);}\n");
  html += F(".wifi-tools{display:flex;flex-wrap:wrap;align-items:center;gap:0.75rem;margin-bottom:1rem;}\n");
  html += F("button.primary{width:100%;padding:0.95rem;background:linear-gradient(135deg,#3478f6,#2746ff);color:#fff;border:none;border-radius:12px;font-size:1.05rem;font-weight:700;cursor:pointer;box-shadow:0 10px 24px rgba(39,70,255,0.35);margin-top:1rem;}\n");
  html += F("button.primary:hover{background:linear-gradient(135deg,#255fcb,#1b34af);}\n");
  html += F("button.secondary{padding:0.65rem 1.1rem;background:#1a2744;color:#d4dcff;border:none;border-radius:10px;font-weight:600;cursor:pointer;transition:background 0.2s ease;}\n");
  html += F("button.secondary:hover{background:#23355c;}\n");
  html += F(".wifi-scan{font-size:0.9rem;color:#d7e3ff;width:100%;}\n");
  html += F(".wifi-scan-entry{padding:0.45rem 0;border-bottom:1px solid rgba(255,255,255,0.08);}\n");
  html += F(".wifi-scan-entry:last-child{border-bottom:none;}\n");
  html += F(".wifi-scan-entry strong{display:flex;align-items:center;gap:0.35rem;}\n");
  html += F(".signal{font-size:1rem;}\n");
  html += F(".wifi-status{min-height:1.2rem;}\n");
  html += F(".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:1rem;font-size:0.95rem;}\n");
  html += F(".status-item{background:#0b1322;border-radius:12px;padding:0.85rem 1rem;border:1px solid rgba(255,255,255,0.05);}\n");
  html += F(".status-item strong{display:block;font-size:0.85rem;text-transform:uppercase;letter-spacing:0.05em;color:#93a5d8;margin-bottom:0.35rem;}\n");
  html += F("ul.tips{margin:0;padding-left:1.25rem;color:#d0dcff;}\n");
  html += F("footer{text-align:center;padding:1.5rem 1rem;color:#8da2d9;font-size:0.85rem;}\n");
  html += F("@media (max-width:640px){.menu-item{flex:1 0 100%;justify-content:center;}.menu{gap:0.5rem;}}\n");
  html += F(".wifi-scan-status{font-size:0.9rem;color:#9bb3ff;min-height:1.2rem;}\n");
  html += F(".wifi-scan-status.scanning{color:#74c0ff;}\n");
  html += F(".wifi-scan-status .pulse{display:inline-block;animation:pulse 1s ease-in-out infinite;}\n");
  html += F("@keyframes pulse{0%,100%{transform:scale(1);}50%{transform:scale(1.2);}}\n");
  html += F("</style></head><body>");

  html += F("<header><h1>PixelEtherLED</h1><p>Panel de configuración avanzada</p></header>");
  html += F("<nav class='menu'>");
  html += F("<a href='#ethernet' class='menu-item ethernet'><span class='icon'>🔌</span><span>Ethernet</span></a>");
  html += F("<a href='#artnet' class='menu-item artnet'><span class='icon'>🎛️</span><span>Art-Net</span></a>");
  html += F("<a href='#wifi' class='menu-item wifi'><span class='icon'>📡</span><span>Wi-Fi</span></a>");
  html += F("<a href='#leds' class='menu-item leds'><span class='icon'>💡</span><span>LEDs</span></a>");
  html += F("<a href='/visualizer' class='menu-item preview'><span class='icon'>🧩</span><span>Visualizador</span></a>");
  html += F("<a href='#estado' class='menu-item system'><span class='icon'>📊</span><span>Estado</span></a>");
  html += F("</nav>");

  html += F("<div class='content'>");
  if (message.length()) {
    html += F("<div class='panel' style='border:1px solid rgba(76,142,255,0.45);background:rgba(37,70,203,0.15);'>");
    html += F("<div class='panel-title'><span class='badge'>✅</span><span>Mensaje del sistema</span></div>");
    html += message;
    html += F("</div>");
  }

  html += F("<form method='post' action='/config'>");

  html += F("<section id='ethernet' class='panel panel-ethernet'>");
  html += F("<h2 class='panel-title'><span class='badge'>🔌</span><span>Ethernet</span></h2>");
  html += F("<label for='dhcpTimeout'>Tiempo de espera DHCP (ms)</label>");
  html += "<input type='number' id='dhcpTimeout' name='dhcpTimeout' min='500' max='60000' value='" + String(config.dhcpTimeoutMs) + "'>";
  html += F("<label for='networkMode'>Modo de red</label>");
  html += F("<select id='networkMode' name='networkMode'>");
  html += String("<option value='dhcp'") + (usingDhcp ? " selected" : "") + ">DHCP (automático)</option>";
  html += String("<option value='static'") + (!usingDhcp ? " selected" : "") + ">IP fija</option>";
  html += F("</select>");
  html += F("<label for='fallbackToStatic'>Si DHCP falla</label>");
  html += F("<select id='fallbackToStatic' name='fallbackToStatic'>");
  html += String("<option value='1'") + (config.fallbackToStatic ? " selected" : "") + ">Aplicar IP fija configurada</option>";
  html += String("<option value='0'") + (!config.fallbackToStatic ? " selected" : "") + ">Mantener sin IP</option>";
  html += F("</select>");
  html += F("<div class='dual'>");
  html += "<div><label for='staticIp'>IP fija</label><input type='text' id='staticIp' name='staticIp' value='" + staticIpStr + "'></div>";
  html += "<div><label for='staticGateway'>Puerta de enlace</label><input type='text' id='staticGateway' name='staticGateway' value='" + staticGwStr + "'></div>";
  html += F("</div>");
  html += F("<div class='dual'>");
  html += "<div><label for='staticMask'>Máscara de subred</label><input type='text' id='staticMask' name='staticMask' value='" + staticMaskStr + "'></div>";
  html += "<div><label for='staticDns1'>DNS primario</label><input type='text' id='staticDns1' name='staticDns1' value='" + staticDns1Str + "'></div>";
  html += F("</div>");
  html += "<label for='staticDns2'>DNS secundario</label><input type='text' id='staticDns2' name='staticDns2' value='" + staticDns2Str + "'>";
  html += F("</section>");

  html += F("<section id='artnet' class='panel panel-artnet'>");
  html += F("<h2 class='panel-title'><span class='badge'>🎛️</span><span>Art-Net</span></h2>");
  html += F("<label for='artnetInput'>Interfaz preferida</label>");
  html += F("<select id='artnetInput' name='artnetInput'>");
  html += String("<option value='0'") + (config.artnetInput == 0 ? " selected" : "") + ">Ethernet</option>";
  html += String("<option value='1'") + (config.artnetInput == 1 ? " selected" : "") + ">Wi-Fi</option>";
  html += String("<option value='2'") + (config.artnetInput == 2 ? " selected" : "") + ">Automático</option>";
  html += F("</select>");
  html += F("<p style='margin:-0.35rem 0 0.9rem;font-size:0.9rem;color:#94a7df;'>Definí desde qué interfaz se reciben los datos Art-Net. Si la opción seleccionada no tiene IP, se usará la otra disponible.</p>");
  html += F("</section>");

  html += F("<section id='wifi' class='panel panel-wifi'>");
  html += F("<h2 class='panel-title'><span class='badge'>📡</span><span>Wi-Fi</span></h2>");
  html += F("<label for='wifiEnabled'>Wi-Fi</label>");
  html += F("<select id='wifiEnabled' name='wifiEnabled'>");
  html += String("<option value='1'") + (wifiEnabled ? " selected" : "") + ">Habilitado</option>";
  html += String("<option value='0'") + (!wifiEnabled ? " selected" : "") + ">Deshabilitado</option>";
  html += F("</select>");
  html += F("<label for='wifiMode'>Modo Wi-Fi</label>");
  html += F("<select id='wifiMode' name='wifiMode'>");
  html += String("<option value='ap'") + (wifiApMode ? " selected" : "") + ">Punto de acceso</option>";
  html += String("<option value='sta'") + (!wifiApMode ? " selected" : "") + ">Cliente (unirse a red)</option>";
  html += F("</select>");
  html += F("<div id='wifiStaConfig' class='wifi-group'>");
  html += F("<label for='wifiStaSsid'>SSID</label>");
  html += "<input type='text' id='wifiStaSsid' name='wifiStaSsid' list='wifiNetworks' value='" + wifiStaSsidEsc + "'>";
  html += F("<datalist id='wifiNetworks'></datalist>");
  html += F("<label for='wifiStaPassword'>Contraseña</label>");
  html += F("<div class='password-field'>");
  html += "<input type='password' id='wifiStaPassword' name='wifiStaPassword' value='" + wifiStaPassEsc + "'>";
  html += F("<button type='button' class='toggle-password' data-target='wifiStaPassword' aria-label='Mostrar contraseña'>👁️</button>");
  html += F("</div>");
  html += F("<div class='wifi-tools'>");
  html += F("<button type='button' class='secondary' id='wifiScanButton'>Escanear redes Wi-Fi</button>");
  html += F("<div id='wifiScanStatus' class='wifi-scan-status wifi-status'></div>");
  html += F("</div>");
  html += F("<div id='wifiScanResults' class='wifi-scan'></div>");
  html += F("</div>");
  html += F("<div id='wifiApConfig' class='wifi-group'>");
  html += F("<label for='wifiApSsid'>SSID del punto de acceso</label>");
  html += "<input type='text' id='wifiApSsid' name='wifiApSsid' value='" + wifiApSsidEsc + "'>";
  html += F("<label for='wifiApPassword'>Contraseña (mínimo 8 caracteres, dejar vacío para abierto)</label>");
  html += F("<div class='password-field'>");
  html += "<input type='password' id='wifiApPassword' name='wifiApPassword' value='" + wifiApPassEsc + "'>";
  html += F("<button type='button' class='toggle-password' data-target='wifiApPassword' aria-label='Mostrar contraseña'>👁️</button>");
  html += F("</div>");
  html += F("<p style='margin:0;font-size:0.85rem;color:#94a7df;'>Los cambios Wi-Fi se aplican inmediatamente al guardar.</p>");
  html += F("</div>");
  html += F("</section>");

  html += F("<section id='leds' class='panel panel-leds'>");
  html += F("<h2 class='panel-title'><span class='badge'>💡</span><span>LEDs</span></h2>");
  html += F("<label for='numLeds'>Cantidad de LEDs activos</label>");
  html += "<input type='number' id='numLeds' name='numLeds' min='1' max='" + String(MAX_LEDS) + "' value='" + String(config.numLeds) + "'>";
  html += F("<label for='startUniverse'>Universo Art-Net inicial</label>");
  html += "<input type='number' id='startUniverse' name='startUniverse' min='0' max='32767' value='" + String(config.startUniverse) + "'>";
  html += F("<label for='pixelsPerUniverse'>Pixeles por universo</label>");
  html += "<input type='number' id='pixelsPerUniverse' name='pixelsPerUniverse' min='1' max='512' value='" + String(config.pixelsPerUniverse) + "'>";
  html += F("<label for='brightness'>Brillo máximo (0-255)</label>");
  html += "<input type='number' id='brightness' name='brightness' min='1' max='255' value='" + String(config.brightness) + "'>";
  html += F("<label for='chipType'>Tipo de chip LED</label>");
  html += F("<select id='chipType' name='chipType'>");
  for (uint8_t i = 0; i < static_cast<uint8_t>(LedChipType::CHIP_TYPE_COUNT); ++i) {
    html += "<option value='" + String(i) + "'";
    if (config.chipType == i) html += " selected";
    html += ">";
    html += CHIP_TYPE_NAMES[i];
    html += F("</option>");
  }
  html += F("</select>");
  html += F("<label for='colorOrder'>Orden de color</label>");
  html += F("<select id='colorOrder' name='colorOrder'>");
  for (uint8_t i = 0; i < static_cast<uint8_t>(LedColorOrder::COLOR_ORDER_COUNT); ++i) {
    html += "<option value='" + String(i) + "'";
    if (config.colorOrder == i) html += " selected";
    html += ">";
    html += COLOR_ORDER_NAMES[i];
    html += F("</option>");
  }
  html += F("</select>");
  html += F("</section>");

  html += F("<button type='submit' class='primary'>Guardar configuración</button>");
  html += F("</form>");

  html += F("<section id='estado' class='panel panel-status'>");
  html += F("<h2 class='panel-title'><span class='badge'>📊</span><span>Estado del sistema</span></h2>");
  html += F("<div class='status-grid'>");
  html += "<div class='status-item'><strong>IP Ethernet</strong>" + runtime.ethLocalIp.toString() + "</div>";
  html += "<div class='status-item'><strong>Link Ethernet</strong>" + String(runtime.ethLinkUp ? "activo" : "desconectado") + "</div>";
  html += "<div class='status-item'><strong>Modo de red</strong>" + String(usingDhcp ? "DHCP" : "IP fija") + "</div>";
  html += "<div class='status-item'><strong>Fallback DHCP</strong>" + fallbackLabel + "</div>";
  html += "<div class='status-item'><strong>Fuente Art-Net</strong>" + artnetInputLabel(config.artnetInput) + "</div>";
  html += "<div class='status-item'><strong>Interfaz activa Art-Net</strong>" + artnetActiveLabel + "</div>";
  html += "<div class='status-item'><strong>IP Art-Net</strong>" + artnetIpStr + "</div>";
  html += "<div class='status-item'><strong>IP fija configurada</strong>" + staticIpStr + "</div>";
  html += "<div class='status-item'><strong>Gateway</strong>" + staticGwStr + "</div>";
  html += "<div class='status-item'><strong>Máscara</strong>" + staticMaskStr + "</div>";
  html += "<div class='status-item'><strong>DNS</strong>" + staticDns1Str + " / " + staticDns2Str + "</div>";
  html += "<div class='status-item'><strong>Wi-Fi</strong>" + wifiStatusText + "</div>";
  html += "<div class='status-item'><strong>Modo Wi-Fi</strong>" + wifiModeLabel + "</div>";
  html += "<div class='status-item'><strong>SSID</strong>" + wifiSsidStatus + "</div>";
  html += "<div class='status-item'><strong>IP Wi-Fi</strong>" + wifiIpStr + "</div>";
  html += "<div class='status-item'><strong>Clientes Wi-Fi</strong>" + wifiClientsStr + "</div>";
  html += "<div class='status-item'><strong>Universos</strong>" + String(runtime.universeCount) + " (desde " + String(config.startUniverse) + ")" + "</div>";
  html += "<div class='status-item'><strong>Frames DMX</strong>" + String((unsigned long)runtime.dmxFrames) + "</div>";
  html += "<div class='status-item'><strong>Brillo</strong>" + String(config.brightness) + "/255" + "</div>";
  html += "<div class='status-item'><strong>DHCP timeout</strong>" + String(config.dhcpTimeoutMs) + " ms" + "</div>";
  html += "<div class='status-item'><strong>Chip LED</strong>" + String(getChipName(config.chipType)) + "</div>";
  html += "<div class='status-item'><strong>Orden</strong>" + String(getColorOrderName(config.colorOrder)) + "</div>";
  html += F("</div></section>");

  html += F("<section class='panel'>");
  html += F("<h2 class='panel-title'><span class='badge'>💡</span><span>Consejos</span></h2>");
  html += F("<ul class='tips'><li>Si ampliás la tira LED, incrementá la <em>Cantidad de LEDs activos</em>.</li><li>Reducí el brillo máximo para ahorrar consumo o evitar saturación.</li><li>Ajustá el tiempo de espera de DHCP si tu red tarda más en asignar IP.</li><li>El valor de pixeles por universo determina cuántos LEDs se controlan por paquete Art-Net.</li><li>Mantené presionado el botón de reinicio durante 10 segundos al encender para restaurar la configuración de fábrica.</li></ul>");
  html += F("</section>");

  html += F("<section class='panel'>");
  html += F("<h2 class='panel-title'><span class='badge'>⬆️</span><span>Actualizar firmware</span></h2>");
  html += F("<form method='post' action='/update' enctype='multipart/form-data'>");
  html += F("<label for='firmware'>Seleccioná el archivo de firmware (.bin)</label>");
  html += F("<input type='file' id='firmware' name='firmware' accept='.bin,application/octet-stream'>");
  html += F("<button type='submit' class='primary'>Subir y aplicar firmware</button>");
  html += F("</form>");
  html += F("<p style='margin-top:0.75rem;font-size:0.9rem;color:#94a7df;'>El dispositivo se reiniciará automáticamente luego de una actualización exitosa.</p>");
  html += F("</section>");

  html += F("</div><footer>PixelEtherLED &bull; Panel de control web</footer>");

static const char PROGMEM kWifiScript[] = R"rawliteral(
<script>
const passwordToggles = document.querySelectorAll('.toggle-password');

passwordToggles.forEach((btn) => {
  const targetId = btn.getAttribute('data-target');
  const input = document.getElementById(targetId);
  if (!input) return;

  function updateState() {
    const showing = input.type === 'text';
    btn.textContent = showing ? '🙈' : '👁️';
    btn.setAttribute('aria-label', showing ? 'Ocultar contraseña' : 'Mostrar contraseña');
    btn.setAttribute('aria-pressed', showing ? 'true' : 'false');
  }

  btn.addEventListener('click', () => {
    input.type = input.type === 'password' ? 'text' : 'password';
    updateState();
  });

  updateState();
});

const wifiEnabledEl = document.getElementById('wifiEnabled');
const wifiModeEl = document.getElementById('wifiMode');
const wifiStaEl = document.getElementById('wifiStaConfig');
const wifiApEl = document.getElementById('wifiApConfig');
const scanBtn = document.getElementById('wifiScanButton');
const wifiScanResults = document.getElementById('wifiScanResults');
const wifiScanStatus = document.getElementById('wifiScanStatus');
const wifiNetworkList = document.getElementById('wifiNetworks');

function updateWifiVisibility() {
  const enabled = wifiEnabledEl.value === '1';
  const mode = wifiModeEl.value;
  wifiStaEl.style.display = (enabled && mode === 'sta') ? 'block' : 'none';
  wifiApEl.style.display = (enabled && mode === 'ap') ? 'block' : 'none';
}

updateWifiVisibility();
wifiEnabledEl.addEventListener('change', updateWifiVisibility);
wifiModeEl.addEventListener('change', updateWifiVisibility);

function setScanStatus(text, scanning) {
  if (!wifiScanStatus) return;
  wifiScanStatus.textContent = '';
  if (scanning) {
    const icon = document.createElement('span');
    icon.className = 'pulse';
    icon.textContent = '📡';
    wifiScanStatus.appendChild(icon);
    wifiScanStatus.appendChild(document.createTextNode(' ' + text));
    wifiScanStatus.classList.add('scanning');
  } else {
    wifiScanStatus.textContent = text;
    wifiScanStatus.classList.remove('scanning');
  }
}

function signalBars(rssi) {
  if (rssi >= -55) return '📶📶📶';
  if (rssi >= -65) return '📶📶';
  if (rssi >= -75) return '📶';
  return '▫️';
}

function scanWifi() {
  if (!wifiScanResults) return;
  wifiScanResults.innerHTML = '';
  setScanStatus('Escaneando redes…', true);
  if (wifiNetworkList) {
    while (wifiNetworkList.firstChild) {
      wifiNetworkList.removeChild(wifiNetworkList.firstChild);
    }
  }

  fetch('/wifi_scan')
    .then(function(res) {
      if (!res.ok) {
        throw new Error('http');
      }
      return res.json();
    })
    .then(function(data) {
      if (!data || !Array.isArray(data.networks) || data.networks.length === 0) {
        setScanStatus('No se encontraron redes.', false);
        return;
      }

      setScanStatus('Redes disponibles', false);
      data.networks.forEach(function(net) {
        var container = document.createElement('div');
        container.className = 'wifi-scan-entry';

        var title = document.createElement('strong');
        title.innerHTML = '<span class=\"signal\">' + signalBars(net.rssi) + '</span>' +
                          (net.ssid && net.ssid.length ? net.ssid : '(sin SSID)');
        container.appendChild(title);

        var details = document.createElement('div');
        details.textContent = 'Señal: ' + net.rssi + ' dBm · ' + net.secure + ' · Canal ' + net.channel;
        container.appendChild(details);

        wifiScanResults.appendChild(container);

        if (wifiNetworkList) {
          var opt = document.createElement('option');
          opt.value = net.ssid || '';
          wifiNetworkList.appendChild(opt);
        }
      });
    })
    .catch(function() {
      setScanStatus('No se pudo completar el escaneo.', false);
    });
}

if (scanBtn) {
  scanBtn.addEventListener('click', scanWifi);
}
</script>
)rawliteral";

  html += FPSTR(kWifiScript);
  html += F("</body></html>");

  return html;
}
String renderVisualizerPage(const AppConfig& config,
                            const WebUiRuntime& runtime)
{
  const String artnetIpStr = (runtime.artnetIp != IPAddress((uint32_t)0)) ? runtime.artnetIp.toString() : String('-');
  const String ethIpStr = runtime.ethLocalIp.toString();
  const IPAddress wifiCurrentIp = runtime.wifiStaHasIp ? runtime.wifiStaIp :
                                  (runtime.wifiApRunning ? runtime.wifiApIp : IPAddress((uint32_t)0));
  const String wifiIpStr = (wifiCurrentIp != IPAddress((uint32_t)0)) ? wifiCurrentIp.toString() : String('-');
  String wifiSsidLabel = runtime.wifiStaSsidCurrent.length() ? runtime.wifiStaSsidCurrent : config.wifiStaSsid;
  wifiSsidLabel.trim();
  const String wifiSsidEsc = wifiSsidLabel.length() ? htmlEscape(wifiSsidLabel) : String(F("(no asociado)"));

  String html;
  html.reserve(16000);
  html += F("<!DOCTYPE html><html lang='es'><head><meta charset='utf-8'>");
  html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
  html += F("<title>PixelEtherLED - Visualizador</title>");
  html += F("<style>:root{color-scheme:dark;}body{font-family:'Segoe UI',Helvetica,Arial,sans-serif;background:#080b14;color:#f0f0f0;margin:0;}\n");
  html += F("header{background:linear-gradient(135deg,#111a30,#0b4bd8);padding:1.75rem;text-align:center;box-shadow:0 8px 20px rgba(0,0,0,0.55);}\n");
  html += F("header h1{margin:0;font-size:2rem;font-weight:700;}header p{margin:0.35rem 0 0;color:#d0dcff;font-size:1rem;}\n");
  html += F(".menu{display:flex;flex-wrap:wrap;justify-content:center;gap:0.75rem;padding:1rem 1.5rem;background:#0d1424;box-shadow:0 6px 18px rgba(0,0,0,0.45);}\n");
  html += F(".menu-item{display:flex;align-items:center;gap:0.75rem;padding:0.75rem 1.25rem;border-radius:12px;font-weight:600;text-decoration:none;color:#fff;box-shadow:0 6px 12px rgba(0,0,0,0.35);transition:transform 0.2s ease,box-shadow 0.2s ease;}\n");
  html += F(".menu-item:hover{transform:translateY(-2px);box-shadow:0 10px 24px rgba(0,0,0,0.45);}\n");
  html += F(".menu-item .icon{font-size:1.4rem;}\n");
  html += F(".menu-item.config-link{background:linear-gradient(135deg,#3478f6,#1d3fbf);}\n");
  html += F(".menu-item.preview{background:linear-gradient(135deg,#4caf50,#2e7d32);}\n");
  html += F(".menu-item.active{outline:2px solid rgba(255,255,255,0.35);outline-offset:2px;}\n");
  html += F(".content{padding:1.5rem;max-width:1100px;margin:0 auto;}\n");
  html += F(".panel{background:#101728;border-radius:16px;padding:1.5rem;margin-bottom:1.5rem;box-shadow:0 16px 32px rgba(0,0,0,0.45);}\n");
  html += F(".panel-title{display:flex;align-items:center;gap:0.6rem;margin:0 0 1.25rem;font-size:1.35rem;font-weight:700;color:#f5f7ff;}\n");
  html += F(".panel-title .badge{font-size:1.5rem;}\n");
  html += F(".grid-controls{display:flex;flex-wrap:wrap;gap:1rem;margin-bottom:1rem;}\n");
  html += F(".grid-controls label{display:flex;flex-direction:column;font-weight:600;font-size:0.95rem;}\n");
  html += F(".grid-controls input{margin-top:0.35rem;padding:0.55rem 0.7rem;border-radius:10px;border:1px solid #23314d;background:#0b1322;color:#f0f0f0;width:120px;}\n");
  html += F(".grid-controls button{padding:0.65rem 1.2rem;background:#1a2744;color:#d4dcff;border:none;border-radius:10px;font-weight:600;cursor:pointer;transition:background 0.2s ease;box-shadow:0 6px 16px rgba(0,0,0,0.35);}\n");
  html += F(".grid-controls button.primary{background:linear-gradient(135deg,#3478f6,#2746ff);color:#fff;}\n");
  html += F(".grid-controls button:hover{background:#23355c;}\n");
  html += F(".grid-controls button.primary:hover{background:linear-gradient(135deg,#255fcb,#1b34af);}\n");
  html += F(".visual-grid{display:grid;gap:0.5rem;justify-content:flex-start;}\n");
  html += F(".pixel-cell{position:relative;width:56px;height:56px;border-radius:10px;border:1px solid rgba(255,255,255,0.08);background:#0b1322;display:flex;align-items:center;justify-content:center;transition:transform 0.15s ease,box-shadow 0.15s ease;}\n");
  html += F(".pixel-cell:hover{transform:translateY(-2px);box-shadow:0 8px 20px rgba(0,0,0,0.45);}\n");
  html += F(".pixel-cell.invalid{outline:2px solid #ff3860;outline-offset:1px;}\n");
  html += F(".pixel-cell input{width:100%;height:100%;border:none;background:transparent;color:inherit;font-weight:700;font-size:0.95rem;text-align:center;appearance:textfield;}\n");
  html += F(".pixel-cell input:focus{outline:none;}\n");
  html += F(".pixel-label{position:absolute;pointer-events:none;font-weight:700;}\n");
  html += F(".preview-actions{display:flex;flex-wrap:wrap;align-items:center;gap:1rem;margin-top:1rem;}\n");
  html += F(".preview-actions button{padding:0.75rem 1.4rem;border:none;border-radius:12px;font-weight:700;cursor:pointer;background:linear-gradient(135deg,#ff8a00,#e52e71);color:#fff;box-shadow:0 10px 24px rgba(229,46,113,0.35);}\n");
  html += F(".preview-actions button:hover{background:linear-gradient(135deg,#e97800,#c8265d);}\n");
  html += F(".preview-status{font-size:0.95rem;color:#d0dcff;}\n");
  html += F(".status-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:1rem;font-size:0.95rem;}\n");
  html += F(".status-item{background:#0b1322;border-radius:12px;padding:0.85rem 1rem;border:1px solid rgba(255,255,255,0.05);}\n");
  html += F(".status-item strong{display:block;font-size:0.85rem;text-transform:uppercase;letter-spacing:0.05em;color:#93a5d8;margin-bottom:0.35rem;}\n");
  html += F(".tips{margin:0.5rem 0 0;color:#9bb3ff;font-size:0.9rem;line-height:1.5;}\n");
  html += F("@media (max-width:640px){.pixel-cell{width:48px;height:48px;}.grid-controls input{width:100px;}}\n");
  html += F("</style></head><body>");

  html += F("<header><h1>PixelEtherLED</h1><p>Visualizador en vivo de Art-Net</p></header>");
  html += F("<nav class='menu'>");
  html += F("<a href='/config' class='menu-item config-link'><span class='icon'>⚙️</span><span>Configuración</span></a>");
  html += F("<a href='/visualizer' class='menu-item preview active'><span class='icon'>🧩</span><span>Visualizador</span></a>");
  html += F("</nav>");

  html += F("<div class='content'>");
  html += F("<section class='panel'><h2 class='panel-title'><span class='badge'>🧱</span><span>Diseña tu cuadrícula</span></h2>");
  html += F("<p class='tips'>Elige el número de filas y columnas que representa tu panel físico. Puedes autocompletar el orden de los LEDs en modo normal o serpentina, y ajustar manualmente cualquier posición.</p>");
  html += F("<div class='grid-controls'>");
  html += F("<label for='gridRows'>Filas<input type='number' id='gridRows' min='1' max='64' value='1'></label>");
  html += F("<label for='gridCols'>Columnas<input type='number' id='gridCols' min='1' max='64' value='1'></label>");
  html += F("<button id='generateGrid' class='primary'>Crear cuadrícula</button>");
  html += F("<button id='autoFillNormal'>Autocompletar (normal)</button>");
  html += F("<button id='autoFillSnake'>Autocompletar (serpentina)</button>");
  html += F("<button id='clearGrid'>Limpiar</button>");
  html += F("</div>");
  html += F("<div id='visualGrid' class='visual-grid' aria-live='polite'></div>");
  html += F("<div class='preview-actions'><button id='togglePreview'>Iniciar vista previa</button><div class='preview-status' id='previewStatus'>Esperando a iniciar…</div></div>");
  html += F("</section>");

  html += F("<section class='panel'><h2 class='panel-title'><span class='badge'>📊</span><span>Estado en tiempo real</span></h2>");
  html += F("<div class='status-grid'>");
  html += F("<div class='status-item'><strong>LEDs configurados</strong><span>");
  html += String(config.numLeds);
  html += F("</span></div>");
  html += F("<div class='status-item'><strong>Universos activos</strong><span>");
  html += String(runtime.universeCount);
  html += F("</span></div>");
  html += F("<div class='status-item'><strong>Frames Art-Net</strong><span id='frameCounter'>");
  html += String(runtime.dmxFrames);
  html += F("</span></div>");
  html += F("<div class='status-item'><strong>Interfaz preferida</strong><span>");
  html += artnetInputLabel(config.artnetInput);
  html += F("</span></div>");
  html += F("<div class='status-item'><strong>IP Ethernet</strong><span>");
  html += ethIpStr;
  html += F("</span></div>");
  html += F("<div class='status-item'><strong>IP Wi-Fi actual</strong><span>");
  html += wifiIpStr;
  html += F("</span></div>");
  html += F("<div class='status-item'><strong>SSID Wi-Fi</strong><span>");
  html += wifiSsidEsc;
  html += F("</span></div>");
  html += F("<div class='status-item'><strong>Último origen Art-Net</strong><span>");
  html += artnetIpStr;
  html += F("</span></div>");
  html += F("</div>");
  html += F("<p class='tips'>La vista previa consulta periódicamente el estado de los LEDs (cada 200 ms) sin interrumpir la reproducción.</p>");
  html += F("</section>");
  html += F("</div>");

  static const char PROGMEM kVisualizerScript[] = R"rawliteral(
<script>
(function(){
  const totalLeds = {{TOTAL_LEDS}};
  const pixelsPerUniverse = {{PIXELS_PER_UNIVERSE}};
  const pollInterval = {{POLL_INTERVAL}};
  const initialFrames = {{INITIAL_FRAMES}};
  const gridRowsInput = document.getElementById('gridRows');
  const gridColsInput = document.getElementById('gridCols');
  const gridContainer = document.getElementById('visualGrid');
  const generateBtn = document.getElementById('generateGrid');
  const autoNormalBtn = document.getElementById('autoFillNormal');
  const autoSnakeBtn = document.getElementById('autoFillSnake');
  const clearBtn = document.getElementById('clearGrid');
  const togglePreviewBtn = document.getElementById('togglePreview');
  const previewStatus = document.getElementById('previewStatus');
  const frameCounter = document.getElementById('frameCounter');
  let previewTimer = null;
  let cells = [];
  let lastFrame = initialFrames;

  function clampValue(value, min, max) {
    value = parseInt(value, 10);
    if (isNaN(value)) return null;
    if (value < min) value = min;
    if (value > max) value = max;
    return value;
  }

  function computeDefaultGrid() {
    const approx = Math.max(1, Math.round(Math.sqrt(totalLeds)));
    const rows = approx;
    const cols = Math.max(1, Math.ceil(totalLeds / rows));
    gridRowsInput.value = rows;
    gridColsInput.value = cols;
  }

  function buildGrid() {
    const rows = clampValue(gridRowsInput.value, 1, 128) || 1;
    const cols = clampValue(gridColsInput.value, 1, 256) || 1;
    gridContainer.innerHTML = '';
    cells = [];
    gridContainer.style.gridTemplateColumns = 'repeat(' + cols + ', minmax(0, 1fr))';
    const total = rows * cols;
    for (let i = 0; i < total; ++i) {
      const cell = document.createElement('div');
      cell.className = 'pixel-cell';
      const input = document.createElement('input');
      input.type = 'number';
      input.min = '0';
      input.max = String(totalLeds - 1);
      input.placeholder = '-';
      const label = document.createElement('span');
      label.className = 'pixel-label';
      label.textContent = '-';
      input.addEventListener('input', function() {
        const value = input.value.trim();
        label.textContent = value.length ? value : '-';
        validateCell(cell, input);
      });
      cell.appendChild(input);
      cell.appendChild(label);
      gridContainer.appendChild(cell);
      cells.push({ wrapper: cell, input: input, label: label });
    }
  }

  function validateCell(cell, input) {
    const value = input.value.trim();
    if (!value.length) {
      cell.classList.remove('invalid');
      return null;
    }
    const parsed = parseInt(value, 10);
    if (isNaN(parsed) || parsed < 0 || parsed >= totalLeds) {
      cell.classList.add('invalid');
      return null;
    }
    cell.classList.remove('invalid');
    return parsed;
  }

  function clearGrid() {
    cells.forEach(function(cell) {
      cell.input.value = '';
      cell.label.textContent = '-';
      cell.wrapper.style.backgroundColor = '#0b1322';
      cell.wrapper.style.color = '#f0f0f0';
      cell.wrapper.classList.remove('invalid');
    });
  }

  function autoFill(serpentine) {
    const rows = clampValue(gridRowsInput.value, 1, 128) || 1;
    const cols = clampValue(gridColsInput.value, 1, 256) || 1;
    let index = 0;
    for (let r = 0; r < rows; ++r) {
      const start = r * cols;
      const end = start + cols;
      const slice = cells.slice(start, end);
      const rowCells = serpentine && (r % 2 === 1) ? slice.slice().reverse() : slice;
      rowCells.forEach(function(cell) {
        if (index < totalLeds) {
          cell.input.value = index;
          cell.label.textContent = index;
        } else {
          cell.input.value = '';
          cell.label.textContent = '-';
        }
        validateCell(cell.wrapper, cell.input);
        ++index;
      });
    }
  }

  function luminanceFromHex(hex) {
    if (!hex || hex.length !== 7) {
      return 0;
    }
    const r = parseInt(hex.substr(1, 2), 16) / 255;
    const g = parseInt(hex.substr(3, 2), 16) / 255;
    const b = parseInt(hex.substr(5, 2), 16) / 255;
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
  }

  function applyColor(cell, hex) {
    const color = hex || '#0b1322';
    cell.wrapper.style.backgroundColor = color;
    const lum = luminanceFromHex(color);
    cell.wrapper.style.color = lum > 0.45 ? '#0b1322' : '#ffffff';
  }

  function collectIndices() {
    return cells.map(function(cell) {
      return validateCell(cell.wrapper, cell.input);
    });
  }

  function setPreviewState(running, message) {
    togglePreviewBtn.textContent = running ? 'Detener vista previa' : 'Iniciar vista previa';
    previewStatus.textContent = message;
  }

  function scheduleNextUpdate() {
    if (previewTimer) {
      clearTimeout(previewTimer);
    }
    previewTimer = setTimeout(fetchPixels, pollInterval);
  }

  function fetchPixels() {
    fetch('/api/led_pixels', { cache: 'no-store' })
      .then(function(response) {
        if (!response.ok) {
          throw new Error('HTTP ' + response.status);
        }
        return response.json();
      })
      .then(function(data) {
        if (!data || !Array.isArray(data.leds)) {
          setPreviewState(true, 'Formato de respuesta desconocido');
          scheduleNextUpdate();
          return;
        }
        const indices = collectIndices();
        cells.forEach(function(cell, idx) {
          const ledIndex = indices[idx];
          if (typeof ledIndex === 'number' && ledIndex < data.leds.length) {
            applyColor(cell, data.leds[ledIndex]);
          } else {
            applyColor(cell, '#0b1322');
          }
        });
        if (typeof data.dmxFrames === 'number') {
          frameCounter.textContent = data.dmxFrames;
          if (data.dmxFrames !== lastFrame) {
            lastFrame = data.dmxFrames;
            previewStatus.textContent = 'Recibiendo datos · Universos: ' + data.universeCount + ' · LEDs por universo: ' + pixelsPerUniverse;
          } else {
            previewStatus.textContent = 'Sin cambios recientes en Art-Net';
          }
        }
        scheduleNextUpdate();
      })
      .catch(function(err) {
        setPreviewState(true, 'Error al consultar datos: ' + err.message);
        scheduleNextUpdate();
      });
  }

  function startPreview() {
    if (previewTimer) {
      return;
    }
    setPreviewState(true, 'Consultando datos de Art-Net…');
    fetchPixels();
  }

  function stopPreview() {
    if (previewTimer) {
      clearTimeout(previewTimer);
      previewTimer = null;
    }
    setPreviewState(false, 'Vista previa detenida');
  }

  generateBtn.addEventListener('click', function() {
    buildGrid();
  });

  autoNormalBtn.addEventListener('click', function() {
    autoFill(false);
  });

  autoSnakeBtn.addEventListener('click', function() {
    autoFill(true);
  });

  clearBtn.addEventListener('click', function() {
    clearGrid();
  });

  togglePreviewBtn.addEventListener('click', function() {
    if (previewTimer) {
      stopPreview();
    } else {
      startPreview();
    }
  });

  computeDefaultGrid();
  buildGrid();
  setPreviewState(false, 'Vista previa detenida');
})();
</script>
)rawliteral";

  String script = FPSTR(kVisualizerScript);
  script.replace(F("{{TOTAL_LEDS}}"), String(config.numLeds));
  script.replace(F("{{PIXELS_PER_UNIVERSE}}"), String(config.pixelsPerUniverse));
  script.replace(F("{{POLL_INTERVAL}}"), String(200));
  script.replace(F("{{INITIAL_FRAMES}}"), String(runtime.dmxFrames));
  html += script;
  html += F("</body></html>");

  return html;
}
