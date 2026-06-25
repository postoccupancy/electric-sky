There are some pieces that live in other repos. They are meant to be re-usable across contexts. 

These include: 
- residentfrequency/signal-router 
- postoccupancy/esp32_ui/src/pages/dashboard/wavelet.tsx (and spectrum.tsx) -- I am choosing to do analysis in React/Next/MUI so it is immediately publishable not buried in a Python notebook 
- postoccupancy/esp32_api/device (current micropython for the running api, not sure what will move or change when this node becomes incorporated into electric-sky, adding bme280 + inmp441 + ina219 but no solar panel, no camera. 

