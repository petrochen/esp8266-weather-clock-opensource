#!/usr/bin/env python3
"""Test actual WiFiManager strings with the shipping stylesheet; no device contacted."""
from pathlib import Path
import argparse
import re,json
from playwright.sync_api import sync_playwright
parser=argparse.ArgumentParser(description='Browser regression of the upstream portal with compact firmware CSS')
parser.add_argument('--wifimanager',type=Path,required=True)
parser.add_argument('--screenshot',type=Path)
args=parser.parse_args()
repo=Path(__file__).resolve().parents[1]
up=(args.wifimanager/'wm_strings_en.h').read_text()
compact=(repo/'firmware/weather_clock/wm_strings_compact.h').read_text()
# Semicolons inside literal text require collecting through the declaration's closing quote.
def string(name,source=up):
 body=re.search(r'const char '+name+r'\[\]\s+PROGMEM\s*=\s*((?:"(?:\\.|[^"\\])*"\s*)+);',source,re.S).group(1)
 return ''.join(json.loads('"'+m+'"') for m in re.findall(r'"((?:\\.|[^"\\])*)"',body))
html=string('HTTP_HEAD_START').replace('{v}','Wi-Fi setup')+string('HTTP_SCRIPT')+string('HTTP_STYLE',compact)+string('HTTP_HEAD_END').replace('{c}','')
for name,locked in [('Home Network',True),('Guest',False),('W'*32,True)]:
 qi=string('HTTP_ITEM_QI').replace('{r}','80').replace('{q}','4').replace('{i}','l' if locked else '').replace('{h}','')
 html+=string('HTTP_ITEM').replace('{V}',name).replace('{v}',name).replace('{qi}',qi).replace('{qp}','')
html+=string('HTTP_FORM_START').replace('{v}','wifisave')+string('HTTP_FORM_WIFI').replace('{v}','SSID').replace('{p}','Password')+string('HTTP_FORM_END')+string('HTTP_SCAN_LINK')+string('HTTP_END')
with sync_playwright() as p:
 browser=p.chromium.launch();page=browser.new_page(viewport={'width':320,'height':700});page.set_content(html)
 page.get_by_role('link',name='Home Network',exact=True).click();assert page.locator('#s').input_value()=='Home Network';assert page.locator('#p').is_enabled()
 page.get_by_role('link',name='Guest',exact=True).click();assert not page.locator('#p').is_enabled()
 page.get_by_role('link',name='Home Network',exact=True).click();page.locator('#showpass').check();assert page.locator('#p').get_attribute('type')=='text'
 if args.screenshot: page.screenshot(path=str(args.screenshot),full_page=True)
 assert page.evaluate('document.documentElement.scrollWidth<=innerWidth')
 browser.close()
print('PASS: upstream portal forms/scripts with compact CSS at320px; locked/open selection and password toggle')
