"""Declared planning assumptions, not a model of suit consumables."""
import math

DEFAULTS = {
    'suit_budget_s': 14400.0, 'v_nom_flat_kmh': 2.0, 'v_return_flat_kmh': 2.0,
    'science_stop_s': 1200.0, 'egress_s': 900.0, 'ingress_s': 900.0,
    'other_tasks_s': 0.0, 'reserve_fixed_s': 1800.0,
    'max_return_s': 3600.0, 'max_return_distance_m': 2000.0,
}
PROFILE = 'LUNAR-ANALOG-2026'


def scenario(overrides=None):
    if overrides is None:
        overrides = {}
    if not isinstance(overrides, dict) or set(overrides) - DEFAULTS.keys():
        raise ValueError('scenario_assumptions debe contener solo parámetros conocidos')
    values = dict(DEFAULTS)
    for key, value in overrides.items():
        if (isinstance(value, bool) or not isinstance(value, (int, float))
                or not 0 <= value <= 1e9 or not math.isfinite(value)):
            raise ValueError(f'valor de escenario inválido: {key}')
        if key in ('v_nom_flat_kmh', 'v_return_flat_kmh') and not 1e-6 <= value <= 1e6:
            raise ValueError(f'valor de escenario inválido: {key}')
        values[key] = value
    return values


def metadata(values, site='Jezero/Mars', build_overrides=None, request_overrides=None):
    if build_overrides is None:
        build_overrides = {key: value for key, value in values.items() if value != DEFAULTS[key]}
    if request_overrides is None:
        request_overrides = {}
    return {
        'profile': PROFILE, 'scenario_id': PROFILE,
        'label': ('Jezero / Marte — supuestos EVA basados en analogía lunar'
                  if site == 'Jezero/Mars' else 'Supuestos EVA basados en analogía lunar'),
        'site': site, 'assumption_basis': 'lunar_analogue',
        'type': 'project_scenario_assumption', 'values': values,
        'defaults': dict(DEFAULTS),
        'overrides': {'build': build_overrides, 'request': request_overrides},
        'units': {key: ('km/h' if key.endswith('_kmh') else 'm' if key.endswith('_m') else 's') for key in DEFAULTS},
        'note': 'Mobility, time and reserve assumptions use lunar analogues for lack of a public Mars standard. Suit budget is a project scenario, not a Mars suit capacity; not NASA GO/NO-GO.',
        'provenance': 'Defaults only: 2 km/h flat is an Artemis ConOps planning assumption/TBR, not a universal average; return speed is a project assumption. 20 min/stop is informed by Apollo station times, not universal. 15+15 min overhead is an optimistic project assumption. Fixed 30 min is an Apollo analog; 60 min / 2 km are lunar-analog constraints. Overrides are user-supplied, not sourced by these analogues.',
        'sensitivity': {'science_stop_s': [600, 2700], 'overhead_total_s': [1800, 3600], 'reserve_fixed_s': [1800, 3600]},
    }


def budget(values, stops, outbound_s, return_s, radial_m, worst_point):
    science = stops * values['science_stop_s']
    overhead = values['egress_s'] + values['ingress_s']
    operational = overhead + outbound_s + science + values['other_tasks_s'] + return_s
    reserve = values['reserve_fixed_s']
    slack = values['suit_budget_s'] - operational - reserve
    status = 'cabe' if slack >= 0 else ('justo' if operational <= values['suit_budget_s'] else 'no_cabe')
    radial_time = radial_m * 3.6 / values['v_return_flat_kmh']
    distance_status = 'exceeds_radial_limit' if radial_m > values['max_return_distance_m'] else 'not_verified'
    time_proxy_status = 'exceeds_proxy_limit' if radial_time > values['max_return_s'] else 'within_proxy_limit'
    terms = {'outbound_travel_s': outbound_s, 'return_travel_s': return_s,
             'science_total_s': science, 'overhead_s': overhead,
             'other_tasks_s': values['other_tasks_s'], 'reserve_fixed_s': reserve}
    largest = max(terms.values())
    return {
        **terms, 'science_stops': stops, 'operational_s': operational,
        'used_s': operational, 'margin_s': values['suit_budget_s'] - operational,
        'reserve_s': reserve, 'slack_s': slack, 'status': status,
        'fits_reserve': status == 'cabe',
        'limiting_terms': [name for name, value in terms.items() if value == largest],
        'return_contingency': {
            'method': 'radial_distance_and_flat_time_proxy', 'worst_point': worst_point,
            'radial_distance_m': radial_m, 'flat_radial_time_s': radial_time,
            'max_distance_m': values['max_return_distance_m'], 'max_time_s': values['max_return_s'],
            'status': distance_status, 'distance_status': distance_status,
            'time_status': 'not_verified', 'time_proxy_status': time_proxy_status,
            'note': 'Straight-line distance is a geometric lower bound, not a traversable route. Flat-speed radial time is a proxy, not a lower bound on terrain-aware return time; exceeding it does not prove a real time violation. Neither check validates return. Independent check, not added to operational time.',
        },
    }
