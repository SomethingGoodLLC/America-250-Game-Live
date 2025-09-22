# Security Guidelines

This document outlines security best practices for the Samson project.

## 🔒 Secrets Management

### Environment Variables
- **Never commit `.env` files** - they are ignored by `.gitignore`
- Use `env.example` files as templates
- Store sensitive configuration in environment variables:
  - `GEMINI_API_KEY` - AI provider API keys
  - `TURN_SERVERS` - WebRTC TURN server credentials
  - Database connection strings
  - SSL certificates and private keys

### API Keys and Credentials
- Store API keys in environment variables, not in code
- Use different keys for development, staging, and production
- Rotate API keys regularly
- Never log or print API keys in application output

### File Exclusions
The following files/directories are automatically ignored:
- `.env*` - Environment files with secrets
- `secrets/` - Directory for secret files
- `*.key`, `*.pem` - Private keys and certificates
- `credentials/` - Credential files
- `models/` - AI model files (may contain sensitive data)

## 🛡️ Content Safety

### Input Validation
- All user input is validated through pydantic models
- Content safety filter applied to all text content
- WebRTC streams are monitored for inappropriate content

### Content Filtering
- Rule-based filtering for hate speech, violence, profanity
- Personal information detection (emails, SSNs, etc.)
- Pluggable provider system for advanced AI-based filtering

## 🔐 Network Security

### WebRTC Security
- STUN/TURN servers configured with authentication
- Media streams encrypted by default
- Peer connection validation and monitoring

### API Security
- CORS properly configured for production
- Request rate limiting (implement in production)
- Input sanitization and validation
- Structured logging without sensitive data

## 📋 Security Checklist

### Development
- [ ] Never commit `.env` files
- [ ] Use environment variables for all secrets
- [ ] Enable content safety filtering
- [ ] Validate all user inputs
- [ ] Use HTTPS in production

### Deployment
- [ ] Set `DEBUG=false` in production
- [ ] Configure proper STUN/TURN servers
- [ ] Set up SSL/TLS certificates
- [ ] Enable request rate limiting
- [ ] Monitor and log security events
- [ ] Regular security updates

### Monitoring
- [ ] Log all authentication attempts
- [ ] Monitor for suspicious content
- [ ] Track API usage patterns
- [ ] Set up alerts for security events
- [ ] Regular security audits

## 🚨 Incident Response

### If Secrets are Compromised
1. **Immediately rotate** all affected API keys
2. **Review logs** for unauthorized access
3. **Update environment variables** in all environments
4. **Notify team members** of the incident
5. **Document the incident** and lessons learned

### Reporting Security Issues
- Report security vulnerabilities privately
- Include detailed reproduction steps
- Provide impact assessment
- Allow reasonable time for fixes before disclosure

## 🔧 Security Tools

### Static Analysis
- `ruff` for code quality and security linting
- `mypy` for type safety
- `bandit` for security-specific Python linting (add to dev dependencies)

### Runtime Security
- Content safety filtering
- Input validation with pydantic
- Structured logging for audit trails
- Session timeout and cleanup

## 📚 Additional Resources

- [OWASP Top 10](https://owasp.org/www-project-top-ten/)
- [FastAPI Security](https://fastapi.tiangolo.com/tutorial/security/)
- [WebRTC Security](https://webrtcsecurity.github.io/)
- [Python Security Best Practices](https://python.org/dev/security/)

Remember: **Security is everyone's responsibility!**
