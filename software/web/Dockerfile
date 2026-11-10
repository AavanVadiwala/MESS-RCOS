FROM node:22-alpine AS frontend
WORKDIR /build/frontend/my-react-app-js
COPY frontend/my-react-app-js/package*.json ./
RUN npm install
COPY frontend/my-react-app-js/ ./
RUN npm run build

FROM node:22-alpine
WORKDIR /app
COPY package*.json ./
RUN npm ci --omit=dev
COPY . .
COPY --from=frontend /build/frontend/my-react-app-js/dist ./dist
ENV FRONTEND_DIR=/app/dist
EXPOSE 3000
CMD ["node", "server.js"]
