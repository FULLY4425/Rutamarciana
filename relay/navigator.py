"""Ephemeral, authenticated phone-to-ESP guidance. Earth GPS stays on the phone."""
import math
import secrets
import threading
import time
import unicodedata
from flask import Blueprint, jsonify, request, Response

STATUSES = {'live', 'simulation', 'waiting', 'paused', 'off_route', 'stale', 'outside'}
KINDS = {'depart', 'left', 'right', 'uturn', 'science', 'destination', 'home', 'straight', 'wait'}
TTL = 1800
FRESH_S = 15


def _number(value, low, high):
    if type(value) not in (int, float) or not math.isfinite(value) or not low <= value <= high:
        raise ValueError('Valor de navegación fuera de rango')
    return value


def validate(body):
    if not isinstance(body, dict):
        raise ValueError('Se requiere objeto JSON')
    status, mode, kind = body.get('status'), body.get('mode'), body.get('kind')
    if status not in STATUSES or mode not in {'training', 'simulation'} or kind not in KINDS:
        raise ValueError('Estado de navegación inválido')
    if (status == 'live' and mode != 'training') or (status == 'simulation' and mode != 'simulation'):
        raise ValueError('Modo y estado incompatibles')
    text = body.get('instruction')
    if not isinstance(text, str) or len(text) > 180:
        raise ValueError('Indicación demasiado larga')
    text = ''.join(c for c in unicodedata.normalize('NFKD', text) if ord(c) < 128 and c.isprintable())
    result = dict(status=status, mode=mode, kind=kind, instruction=text,
                  distance_m=_number(body.get('distance_m', 0), 0, 1e8),
                  remaining_m=_number(body.get('remaining_m', 0), 0, 1e8),
                  accuracy_m=_number(body.get('accuracy_m', 0), 0, 1e6),
                  fix_age_ms=_number(body.get('fix_age_ms', 0), 0, 1e9))
    routes = body.get('budget')
    if routes is not None:
        if not isinstance(routes, dict):
            raise ValueError('Presupuesto inválido')
        result['budget'] = {k: _number(routes.get(k), 0, 1e8) for k in
                            ('fast_out_s', 'fast_back_s', 'safe_out_s', 'safe_back_s', 'dwell_s')}
        stops = routes.get('stops')
        if type(stops) is not int or not 0 <= stops <= 18:
            raise ValueError('Número de paradas inválido')
        result['budget']['stops'] = stops
    return result


def register_navigator(app, clock=time.monotonic):
    bp = Blueprint('navigator', __name__)
    sessions = {}
    lock = threading.Lock()

    @bp.after_request
    def private(response):
        response.headers['Cache-Control'] = 'no-store'
        response.headers['Referrer-Policy'] = 'no-referrer'
        return response

    def authorized(session, role):
        provided = request.headers.get('Authorization', '')
        return secrets.compare_digest(provided, 'Bearer ' + session[role])

    def prune():
        for ident in list(sessions):
            if sessions[ident]['expires'] <= clock():
                del sessions[ident]

    @bp.post('/api/navigator/sessions')
    def create():
        with lock:
            prune()
            if len(sessions) >= 64:
                return jsonify(error='Demasiadas sesiones; espera a que expiren'), 429
            ident = secrets.token_hex(8)
            session = dict(read=secrets.token_hex(16), write=secrets.token_hex(16),
                           expires=clock() + TTL, updated=clock(), seq=0, value=None)
            sessions[ident] = session
            return jsonify(id=ident, read_token=session['read'], write_token=session['write'], expires_in_s=TTL), 201

    @bp.route('/api/navigator/sessions/<ident>', methods=['GET', 'PUT', 'DELETE'])
    def exchange(ident):
        with lock:
            prune()
            session = sessions.get(ident)
            if session is None:
                return jsonify(error='Sesión inexistente o caducada; vuelve a vincular'), 404
            role = 'read' if request.method == 'GET' else 'write'
            if not authorized(session, role):
                return jsonify(error='Credencial inválida'), 403
            if request.method == 'DELETE':
                del sessions[ident]
                return jsonify(ok=True)
            if request.method == 'PUT':
                if request.content_length and request.content_length > 4096:
                    return jsonify(error='Mensaje demasiado grande'), 413
                try:
                    value = validate(request.get_json(silent=True))
                except (ValueError, TypeError) as exc:
                    return jsonify(error=str(exc)), 400
                session.update(value=value, updated=clock(), seq=session['seq'] + 1)
                return jsonify(ok=True, seq=session['seq'])
            elapsed = (clock() - session['updated']) * 1000
            value = dict(session['value'] or dict(status='waiting', mode='training', kind='wait',
                         instruction='Esperando al celular', distance_m=0, remaining_m=0, accuracy_m=0, fix_age_ms=0))
            age = value['fix_age_ms'] + elapsed
            # Both live GPS and simulated playback must have a recent publisher.
            if elapsed > FRESH_S * 1000 or (value['mode'] == 'training' and age > FRESH_S * 1000):
                value.update(status='stale', kind='wait', instruction='Sin posicion reciente. Detente y revisa el celular.', distance_m=0)
            if value['mode'] == 'training' and value['status'] == 'live' and value['accuracy_m'] > 25:
                value.update(status='waiting', kind='wait', instruction='GPS impreciso. Espera una senal mejor.', distance_m=0)
            value.update(seq=session['seq'], age_ms=round(age))
            if request.args.get('format') == 'esp':
                b = value.get('budget', {})
                fields = ['RUTA1', value['seq'], value['mode'], value['status'], value['kind'],
                          round(value['distance_m']), round(value['remaining_m']), round(value['accuracy_m']),
                          value['age_ms'], value['instruction']]
                fields += [b.get(k, -1) for k in ('fast_out_s', 'fast_back_s', 'safe_out_s', 'safe_back_s', 'stops', 'dwell_s')]
                return Response('\n'.join(str(v) for v in fields) + '\n', mimetype='text/plain')
            return jsonify(value)

    app.register_blueprint(bp)
