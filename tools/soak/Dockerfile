# Soak monitor for the RNET Bridge (SPEC §9 M8). Build from the repo root:
#   docker build -t rnet-soak -f tools/soak/Dockerfile tools
# Run (results in /data):
#   docker run -d --name rnet-soak --restart unless-stopped \
#     -e BRIDGE_HOST=10.0.0.165 -v /srv/rnet-soak:/data rnet-soak
FROM python:3.14-slim

RUN pip install --no-cache-dir aiohttp==3.14.5 \
    && useradd --system --uid 10001 soak \
    && mkdir /data && chown soak /data

COPY soak_monitor.py /app/soak_monitor.py

USER soak
ENV OUT_DIR=/data PYTHONUNBUFFERED=1
VOLUME ["/data"]
CMD ["python", "/app/soak_monitor.py"]
