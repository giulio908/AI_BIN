#Librerie necessarie:
#per la webcam:
import cv2
from PIL import Image
#per il modello:
import torch
import torch.nn.functional as F
from torchvision import transforms
#per i gpio:
import RPi.GPIO as GPIO
#Altro:
from time import sleep

#Mnemonici Pin:
PIN_ACK = 17   
PIN_BIT2 = 27 
PIN_BIT1 =22
PIN_BIT0= 23
PIN_REQ=24

#Variabili:
temp0=0
temp1=0
temp2=0

GPIO.setmode(GPIO.BCM)
#Configurazione dei PIN
GPIO.setup(PIN_ACK, GPIO.OUT)
GPIO.setup(PIN_BIT0, GPIO.OUT)
GPIO.setup(PIN_BIT1, GPIO.OUT)
GPIO.setup(PIN_BIT2, GPIO.OUT)
GPIO.setup(PIN_REQ, GPIO.IN)
GPIO.output(PIN_ACK, GPIO.HIGH)

#Preparazione del modello:
PERCORSO_MODELLO = './APC/modello_rifiuti_v3.pt' 
CLASSI =  ['glass', 'metal', 'organic', 'paper', 'plastic']
PERCORSO_SALVATAGGIO = 'res.jpg'
device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
print(f"Caricamento del modello su {device}...")
model = torch.jit.load(PERCORSO_MODELLO, map_location=device)
model.eval()
data_transforms = transforms.Compose([
    transforms.Resize(256),
    transforms.CenterCrop(224),
    transforms.ToTensor(),
    transforms.Normalize([0.485, 0.456, 0.406], [0.229, 0.224, 0.225])
])

#CICLO INFINITO, in cui si attendono le richieste del raspberry:
while True:
    #Se il segnale di REQ si abbassa significa che è arrivata una richiesta.
    if not GPIO.input(PIN_REQ):
        print("req ricevuta")

        #Ricevuta la richiesta rialziamo il segnale ACK
        GPIO.output(PIN_ACK, GPIO.HIGH)

        #ricevuta la richiesta attendiamo 2.5s per dare il tempo di rimuovere la mano dal piatto
        sleep(2.5)
        #avvio webcam
        cap = cv2.VideoCapture(0)
        if not cap.isOpened():
            print("Errore: Impossibile accedere alla webcam.")
            exit()
        #dopo aver avviato la webcam, attendiamo mezzo secondo affinche possa mettere a fuoco.
        sleep(0.5)
        # Cattura di un singolo fotogramma dalla webcam
        ret, frame = cap.read()

        #Gestione della dimensione e ritaglio dell'immagine in modo
        #che venga dato in inferenza solo ciò sul piatto.
        #troviamo il centro dell'immagine:
        h, w, _ = frame.shape
        side = min(h, w)
        # Coordinate del centro
        cx, cy = w // 2, h // 2
        #Coordinate del crop
        x1 = cx - 290 // 2
        y1 = cy - 350 // 2
        x2 = cx + 370 // 2
        y2 = cy + 300 // 2
        #Ritaglio immagine
        frame = frame[y1:y2, x1:x2]

        # Conversione formato colori: OpenCV usa il formato colori BGR, ma PyTorch si aspetta RGB.
        frame_rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        img_pil = Image.fromarray(frame_rgb)
        img_tensor = data_transforms(img_pil).unsqueeze(0).to(device)

        #Predizione:
        with torch.no_grad():
            outputs = model(img_tensor)            
            # Calcolo della probabilità con Softmax
            probabilita = F.softmax(outputs, dim=1)[0]            
            #Preleviamo la probabilità più alta e il suo indice.
            valore_max, indice_predetto = torch.max(probabilita, 0)
            
        # Estraiamo il nome della classe e la percentuale
        classe_predetta = CLASSI[indice_predetto.item()]
        percentuale = valore_max.item() * 100

        if(classe_predetta=='plastic' or classe_predetta=='metal'):
            classe_predetta='plast/met'

        #CODIFICHIAMO LA CLASSE PREDETTA
        match classe_predetta:
            case 'paper':
                temp0=1
                temp1=0
                temp2=0
            case 'plast/met':
                temp0=0
                temp1=1
                temp2=0
            case 'glass':
                temp0=1
                temp1=1
                temp2=0
            case 'organic':
                temp0=0
                temp1=0
                temp2=1
            case _:
                temp0=0
                temp1=0
                temp2=0
        #Stampa debug
        print(str(temp2)+"-"+str(temp1)+"-"+str(temp0))
        print(classe_predetta)

        #Scrittura della codifica sul BUS
        GPIO.output(PIN_BIT0, temp0)
        GPIO.output(PIN_BIT1, temp1)
        GPIO.output(PIN_BIT2, temp2)

        #il dato è pronto e si abbassa il segnale di ACK
        GPIO.output(PIN_ACK, GPIO.LOW)
        print("ho abbassato ack")


        #Salviamo l'immagine catturata e data in inferenza come debug e poter capire
        #cosa il modello vede.
        salvato = cv2.imwrite(PERCORSO_SALVATAGGIO, frame)
        if salvato:
            print(f"Frame salvato in: {PERCORSO_SALVATAGGIO}")
        else:
            print("Errore: impossibile salvare il frame.")

        #Chiusura webcam e finestra
        cap.release()
        cv2.destroyAllWindows()

        