#!/usr/bin/env python3
"""Fetch PokeAPI data for Galar (810-898) and Paldea (906-1025)."""
import json, os, sys, time, urllib.request

CACHE = 'cache/pokeapi'
IDS = list(range(810, 899)) + list(range(906, 1026))

def get(url):
    fn = os.path.join(CACHE, url.replace('https://pokeapi.co/api/v2/', '').replace('/', '_') + '.json')
    if os.path.exists(fn):
        return json.load(open(fn))
    for attempt in range(5):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'TamaPoke-dexgen/1.0'})
            with urllib.request.urlopen(req, timeout=30) as r:
                data = json.load(r)
            os.makedirs(CACHE, exist_ok=True)
            json.dump(data, open(fn, 'w'))
            return data
        except Exception as e:
            print('retry', url, e); time.sleep(2 * (attempt + 1))
    raise RuntimeError('failed ' + url)

out = {}
for i, did in enumerate(IDS):
    p = get(f'https://pokeapi.co/api/v2/pokemon/{did}/')
    s = get(f'https://pokeapi.co/api/v2/pokemon-species/{did}/')
    chain_url = s['evolution_chain']['url']
    c = get(chain_url)
    out[str(did)] = {
        'name': p['name'],
        'types': [t['type']['name'] for t in p['types']],
        'stats': {st['stat']['name']: st['base_stat'] for st in p['stats']},
        'legendary': s['is_legendary'],
        'mythical': s['is_mythical'],
        'evolution_chain': c,
    }
    # learnset: level-up moves with levels per version group
    learn = {}
    for mv in p['moves']:
        for v in mv['version_group_details']:
            if v['move_learn_method']['name'] == 'level-up':
                learn.setdefault(mv['move']['name'], {})[v['version_group']['name']] = v.get('level') or 0
    out[str(did)]['learnset_raw'] = learn
    if (i + 1) % 20 == 0:
        print(f'{i+1}/{len(IDS)}', flush=True)

json.dump(out, open('cache/pokedata.json', 'w'), indent=1)
print('saved', len(out))
