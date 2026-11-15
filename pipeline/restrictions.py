"""User-selected educational restrictions. Never modify stored terrain arrays."""
import math
import numpy as np

FACTORS={'clear':1.0,'dust':0.8,'storm':0.6}


def apply_environment(arrays,environment,cell_m,cell_x_m):
    if not isinstance(environment,dict):raise ValueError('environment debe ser un objeto.')
    weather=environment.get('weather','clear')
    if weather not in FACTORS:raise ValueError('Escenario de polvo desconocido.')
    zones=environment.get('blocked_zones',[])
    if not isinstance(zones,list) or len(zones)>8:raise ValueError('Usa como máximo ocho zonas hipotéticas.')
    dem,slope,rough=arrays
    rr,cc=np.indices(dem.shape);blocked=np.zeros(dem.shape,dtype=bool);normalized=[]
    for zone in zones:
        if not isinstance(zone,dict):raise ValueError('Zona inválida.')
        center=zone.get('center');radius=zone.get('radius_m')
        if (not isinstance(center,list) or len(center)!=2 or any(type(v) not in (int,float) or not math.isfinite(v) for v in center)
            or not 0<=center[0]<dem.shape[0] or not 0<=center[1]<dem.shape[1]
            or type(radius) not in (int,float) or not math.isfinite(radius) or not 0<radius<=math.hypot(dem.shape[0]*cell_m,dem.shape[1]*cell_x_m)):
            raise ValueError('Centro o radio de zona fuera del terreno.')
        blocked |= np.hypot((rr-center[0])*cell_m,(cc-center[1])*cell_x_m)<=radius
        record={'center':list(center),'radius_m':radius}
        if 'kind' in zone:
            if zone['kind'] not in ('manual_obstacle','hypothetical_dust'):raise ValueError('Tipo de exclusión desconocido.')
            record['kind']=zone['kind']
        normalized.append(record)
    result=tuple(np.where(blocked,np.nan,array) for array in arrays) if normalized else arrays
    return result,{'weather':weather,'blocked_zones':normalized,'speed_factor':FACTORS[weather],
      'source':'user_hypothetical_training_scenario','note':'Hypothetical dust and restrictions, not observed Mars weather.'}
