FROM python:3.13-slim

WORKDIR /app
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY *.py ./

# Persistente Dinge (Geräteidentität, .env) werden als Volume gemountet,
# damit Updates die Pairing-Identität nicht verlieren (siehe DEPLOY.md).
EXPOSE 8765
CMD ["python", "neopuck_openai_bridge.py"]
