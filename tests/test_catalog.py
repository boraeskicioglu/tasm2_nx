"""Exercise the production C converter, with synthetic data only."""
import json,subprocess,tempfile,pathlib
root=pathlib.Path(__file__).resolve().parents[1]
exe=root/'build/host-tests/catalog_test'
item={'name':'Example "escaped" name','description':'Text\nwith & characters','entry_id':'test.product','billing_methods':[{'price':'1.99','display_price':'$1.99','currency':'USD'}]}
def run(data,shop=False):
 with tempfile.NamedTemporaryFile(mode='w',delete=False,suffix='.json') as f:
  f.write(data);name=f.name
 try:return json.loads(subprocess.check_output([str(exe),name]+(["shop"] if shop else []),text=True))
 finally:pathlib.Path(name).unlink()
out=run(json.dumps({'items':[item]*18}))['store_items']
assert len(out)==19
assert out[-1]["productId"]=="Scarlet_Spider_suit_offline" and out[-1]["price_amount_micros"]==10000
assert out[0]=={'type':'inapp','price_amount_micros':1990000,'title':item['name'],'price':'$1.99','description':item['description'],'productId':'test.product','price_currency_code':'USD'}
for data in ['{','{}','{"items":{}}','{"items":[{}]}','{"items":[{"billing_methods":[]}]}']:
 assert run(data)=={'store_items':[]}
print('catalog: 18 original products plus local Scarlet descriptor, escaped strings, price conversion and invalid input passed')

bad=dict(item);bad["billing_methods"]=[dict(item["billing_methods"][0],price="not a price")]
assert run(json.dumps({"items":[bad]}))=={"store_items":[]}

original={'items':[item]*18,'store_info':{'fixture':'preserved'}}
shop=run(json.dumps(original),True)
assert shop['items'][:18]==original['items'] and shop['store_info']==original['store_info']
local=shop['items'][-1]
assert len(shop['items'])==19 and local['item']=='Suit_Sca' and local['quantity']==1
assert local['entry_id']=='Scarlet_Spider_suit_offline' and local['billing_methods'][0]['price']==0.01
assert run('{}',True) is None and run('{',True) is None
print('shop catalog: original fields preserved; local Scarlet descriptor and metadata agree; malformed catalog fails closed')
