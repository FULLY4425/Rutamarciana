"""Bounded, temporary mission exchange for the supplied Mars OS, Glen Torridon only.
Four-character codes are public sharing codes, never phone GPS session credentials.
"""
import copy
import json
import math
import re
import secrets
import threading
import time
import zlib
from pathlib import Path
from flask import Blueprint, jsonify, request

ROOT = Path(__file__).resolve().parents[1]
GLEN = ROOT / 'assets/glen-torridon'
TTL = 24 * 3600
CAPACITY = 128


def normalize(body):
    if not isinstance(body, dict) or body.get('region', 'glen-torridon') != 'glen-torridon':
        raise ValueError('Mars OS sin conexión admite solamente Glen Torridon.')
    stops = body.get('stops')
    if not isinstance(stops, list) or not 2 <= len(stops) <= 10:
        raise ValueError('Mars OS admite de 2 a 10 puntos; la web mantiene su límite de 20.')
    limit = body.get('maxSlopeDegrees', 20)
    if type(limit) not in (int, float) or not math.isfinite(limit) or not 5 <= limit <= 40:
        raise ValueError('Pendiente máxima: 5–40 grados.')
    meta = json.loads((GLEN / 'routes.json').read_text())
    spatial, grid = meta['spatial'], meta['grid']
    targets = json.loads((GLEN / 'targets.json').read_text())
    names = {p['name']: p for p in targets}
    normalized = []
    for stop in stops:
        custom = not isinstance(stop, str)
        p = stop if custom else names.get(stop)
        if not isinstance(p, dict) or not isinstance(p.get('name'), str) or not 1 <= len(p['name']) <= 27:
            raise ValueError('Objetivo desconocido o nombre de más de 27 caracteres.')
        lat, lon = p.get('lat'), p.get('lon')
        if any(type(v) not in (int, float) or not math.isfinite(v) for v in (lat, lon)):
            raise ValueError('Coordenadas no válidas.')
        x = math.floor((lon - spatial['west_deg']) / spatial['degree_step'] + .5)
        y = math.floor((spatial['north_deg'] - lat) / spatial['degree_step'] + .5)
        if not 0 <= x < grid['cols'] or not 0 <= y < grid['rows']:
            raise ValueError('El punto queda fuera del mapa sin conexión de Glen Torridon.')
        normalized.append(dict(name=p['name'], x=x, y=y, custom=custom,
                               lat=spatial['north_deg']-y*spatial['degree_step'], lon=spatial['west_deg']+x*spatial['degree_step']))
    if len({(p['x'], p['y']) for p in normalized}) != len(normalized):
        raise ValueError('Puntos duplicados en la misma celda.')
    environment = body.get('environment', {'weather': body.get('weather', 'clear')})
    if not isinstance(environment, dict):
        raise ValueError('Entorno no válido.')
    if not isinstance(environment.get('weather','clear'),str) or environment.get('weather','clear') not in ('clear','dust','storm'):
        raise ValueError('Escenario de polvo desconocido.')
    if environment.get('weather', 'clear') == 'storm' and 'blocked_zones' not in environment:
        environment = {**environment, 'blocked_zones': [dict(center=[int(grid['rows']*.55), int(grid['cols']*.55)], radius_m=max(30, meta['cell_m']*3), kind='hypothetical_dust')]}
    return normalized, limit, environment


def register_psion(app, compute_body, clock=time.monotonic):
    bp = Blueprint('psion', __name__)
    missions, lock = {}, threading.Lock()

    @bp.after_request
    def no_cache(response):
        response.headers['Cache-Control'] = 'no-store'
        return response

    @bp.get('/api/health')
    def health():
        return jsonify(ok=True, project='Ruta Marciana', psion=1, region='glen-torridon')

    @bp.post('/api/psion/missions')
    def share():
        if request.content_length and request.content_length > 12000:
            return jsonify(error='Misión demasiado grande.'), 413
        try:
            stops, limit, environment = normalize(request.get_json(silent=True))
        except (ValueError, TypeError) as exc:
            return jsonify(error=str(exc)), 400
        points = [[p['y'], p['x']] for p in stops]
        result = app.make_response(compute_body(dict(start=points[0], end=points[-1], science_waypoints=points[1:-1],
                                                   max_slope_deg=limit, environment=environment), GLEN))
        data = result.get_json()
        if result.status_code != 200:
            return result
        outbound = data['fast']['outbound']
        payload = dict(region='glen-torridon', stops=stops, maxSlopeDegrees=limit, weatherKey=data['environment']['weather'],
                       environment=data['environment'], distance=outbound['distance_m'], ascent=sum(leg['ascent_m'] for leg in outbound['legs']),
                       minutes=math.ceil(outbound['time_s']/60), cells=len(outbound['points']),
                       pathCrc=f"{zlib.crc32(bytes(v for row,col in outbound['points'] for v in (col,row))):08x}",
                       planner='Ruta Marciana FAST (Python)', comparison='Local watch planner may differ; not an equivalence guarantee.', ttl_seconds=TTL)
        spatial = data['spatial']
        payload.update(weather=data['environment']['weather'], sourceType='synthetic',
                       label='Escenario educativo', notice='No es meteorología observada.',
                       speedKmH=2*data['environment']['speed_factor'],
                       restrictions=[dict(name=z.get('kind','manual_obstacle'),
                           lat=spatial['north_deg']-z['center'][0]*spatial['degree_step'],
                           lon=spatial['west_deg']+z['center'][1]*spatial['degree_step'], radiusMeters=z['radius_m'])
                           for z in data['environment']['blocked_zones']])
        with lock:
            for code in list(missions):
                if missions[code][0] <= clock():
                    del missions[code]
            if len(missions) >= CAPACITY:
                return jsonify(error='Buzón lleno; espera a que caduquen misiones.'), 429
            alphabet = '23456789ABCDEFGHJKLMNPQRSTUVWXYZ'
            code = ''.join(secrets.choice(alphabet) for _ in range(4))
            while code in missions:
                code = ''.join(secrets.choice(alphabet) for _ in range(4))
            payload['code'] = code
            missions[code] = (clock()+TTL, copy.deepcopy(payload))
        return jsonify(payload)

    @bp.get('/api/psion/missions/<code>')
    def receive(code):
        if not re.fullmatch('[A-Z2-9]{4}', code):
            return jsonify(error='Código no válido.'), 404
        with lock:
            entry = missions.get(code)
            if not entry or entry[0] <= clock():
                missions.pop(code, None)
                return jsonify(error='Misión inexistente o caducada.'), 404
            return jsonify(copy.deepcopy(entry[1]))

    app.register_blueprint(bp)
