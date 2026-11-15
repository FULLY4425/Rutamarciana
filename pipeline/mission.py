"""Bounded science intent and explicit user mobility assumptions."""
import math

def mission_context(value):
    if value is None:value={}
    if not isinstance(value,dict):raise ValueError('mission debe ser un objeto')
    def text(key,limit,default=''):
        result=value.get(key,default)
        if not isinstance(result,str) or len(result)>limit:raise ValueError(f'mission.{key}: texto de hasta {limit} caracteres')
        return result.strip()
    def number(key,default,low,high):
        result=value.get(key,default)
        if type(result) not in (int,float) or not math.isfinite(result) or not low<=result<=high:
            raise ValueError(f'mission.{key}: valor fuera de rango')
        return result
    return dict(science_goal=text('science_goal',600),hypothesis=text('hypothesis',600),
        equipment=text('equipment',300),equipment_mass_kg=number('equipment_mass_kg',0,0,100),
        mobility_factor=number('mobility_factor',1,.5,1),crew_size=2,
        mobility_basis='user_selected_training_assumption_not_physiological_model',
        note='Equipment mass is descriptive only; only the explicit mobility factor scales travel speeds. No real consumables or load physiology modeled.')
