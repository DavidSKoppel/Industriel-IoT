# Passwordpolitik – CR 1.7

Politikken kræver mindst 15 tegn og afviser svage passwords med PAM/pwquality.
Den gælder lokale Linux-konti, ikke automatisk MQTT- eller QuestDB-konti.
Eksisterende passwords ændres ikke automatisk.

## Prøv i Docker Desktop

Kør fra denne mappe:

```bash
docker build -t cr17-lab:1.0 .
docker run -d --name cr17-lab cr17-lab:1.0
docker exec cr17-lab python3 /opt/cr17/test_policy.py
docker exec -it cr17-lab passwd cr17test
```

Testscriptet opretter testkontoen og bruger offentlige testpasswords.
Brug dem ikke til rigtige brugere. Windows' egne konti påvirkes ikke.

## På Webdock

Politikken er allerede installeret. Skift dit eget Linux-password med:

```bash
passwd
```

Indtast dit nuværende password og derefter det nye to gange. Tegnene vises ikke under indtastning.
Vælg et langt, unikt password. En svag adgangskode bliver afvist.

`webdock/` indeholder det tidligere installationsscript og kontrolscript.
Kør ikke deployment igen på den nuværende server. Installationen kræver den
eksisterende PAM/pwquality-opsætning og tager backup før ændringer.
Politikken er ikke en fuldstændig implementering af alle IEC 62443-krav.
