/* Presentation and browser speech adapter for simulated route navigation. */
(function () {
  'use strict';
  const el=id=>document.getElementById(id);
  const speech=window.speechSynthesis;
  const supported=Boolean(speech && window.SpeechSynthesisUtterance);
  let data=null, profile=null, plan={steps:[],total:0}, position=null;
  let playing=false, enabled=false, spokenKey=null;
  const format=m=>m>=1000?`${(m/1000).toFixed(1)} km`:`${Math.round(m)} m`;
  const stop=()=>{if(supported)speech.cancel();spokenKey=null;};
  function speak(text) {
    if(!supported)return;
    speech.cancel();
    const utterance=new window.SpeechSynthesisUtterance(text);
    utterance.lang='es-MX';utterance.rate=1;
    utterance.voice=speech.getVoices().find(v=>v.lang==='es-MX') || speech.getVoices().find(v=>v.lang.startsWith('es')) || null;
    utterance.onerror=event=>{if(!['canceled','interrupted'].includes(event.error))el('voicestatus').textContent='No se pudo reproducir la voz. Las indicaciones escritas siguen disponibles.';};
    speech.speak(utterance);
  }
  function instruction() {
    if(!position)return '';
    if(!position.next)return position.step.text;
    return `${position.step.text} En ${format(position.distance)}, ${position.next.text.charAt(0).toLowerCase()+position.next.text.slice(1)}`;
  }
  function update(nextData, nextProfile, sample, isPlaying, scale) {
    if(data!==nextData || profile!==nextProfile) {
      data=nextData;profile=nextProfile;plan=window.MissionNavigation.build(data?.[profile],scale);stop();el('directionslist').dataset.key='';
    }
    if(playing && !isPlaying) stop();
    playing=isPlaying;
    position=sample?window.MissionNavigation.atPosition(plan,sample.distance):null;
    el('repeatdirection').disabled=!position || !supported;
    if(!position) {
      el('directiontext').textContent='Calcula una ruta para recibir indicaciones.';
      el('directiondistance').textContent='—';el('directionsymbol').textContent='↑';
      el('directionremaining').textContent='';el('directionslist').replaceChildren();el('directionslist').dataset.key='';return;
    }
    const upcoming=position.next || position.step;
    el('directionsymbol').textContent=upcoming.symbol;
    el('directiondistance').textContent=position.next?`En ${format(position.distance)}`:'Recorrido completado';
    el('directiontext').textContent=upcoming.text;
    el('directionremaining').textContent=`${format(position.remaining)} para terminar · ${profile.toUpperCase()}`;
    const list=el('directionslist');
    // Update instruction list only when advancing to a new maneuver.
    const listKey=`${profile}:${position.current}:${plan.total}`;
    if(list.dataset.key!==listKey) {
      list.replaceChildren();list.dataset.key=listKey;
      for(const step of plan.steps.slice(position.current,position.current+6)) {
        const item=document.createElement('li');item.textContent=`${format(step.at)} · ${step.text}`;list.appendChild(item);
      }
    }
    if(playing && enabled) {
      const key=`${position.current}:${position.next && position.distance<=30?'near':'ahead'}`;
      if(key!==spokenKey) {spokenKey=key;speak(instruction());}
    }
  }
  el('voice').disabled=!supported;
  el('voice').setAttribute('aria-pressed','false');
  el('voicestatus').textContent=supported?'Voz en español del navegador. Actívala para escuchar durante la simulación.':'Este navegador no ofrece síntesis de voz. Usa las indicaciones escritas.';
  el('voice').onclick=()=>{
    enabled=!enabled;stop();el('voice').setAttribute('aria-pressed',String(enabled));
    el('voice').textContent=enabled?'Voz activada':'Activar voz';
    el('voicestatus').textContent=enabled?'Guía por voz activada · simulación. La voz disponible depende del navegador.':'Guía por voz desactivada.';
    if(enabled && position && playing){spokenKey=`${position.current}:${position.next && position.distance<=30?'near':'ahead'}`;speak(instruction());}
  };
  el('repeatdirection').onclick=()=>{if(position)speak(instruction());};
  window.addEventListener('pagehide',stop);
  window.MissionGuidance={update,stop};
})();
