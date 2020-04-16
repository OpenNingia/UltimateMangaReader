#include "mangaowl.h"

MangaOwl::MangaOwl(DownloadManager *dm) : AbstractMangaSource(dm)
{
    name = "MangaOwl";
    baseurl = "https://mangaowl.net/";
    dicturl = "https://mangaowl.net/list/";
}

bool MangaOwl::uptareMangaList(UpdateProgressToken *token)
{
    QRegularExpression numpagesrx(R"(>(\d+)</a>\s*</li>\s*<li>\s*<a[^>]*?rel="next")");

    QRegularExpression mangarx(
        R"lit(<a href="(https://mangaowl.(?:net|com)/single/[^"]+)">.*?</td>.*?<td>([^<]+)</td>)lit",
        QRegularExpression::DotMatchesEverythingOption);

    MangaList mangas;

    auto job = downloadManager->downloadAsString(dicturl + "1");

    if (!job->await(7000))
    {
        token->sendError(job->errorString);
        return false;
    }

    token->sendProgress(10);

    QElapsedTimer timer;
    timer.start();

    auto numpagesrxmatch = numpagesrx.match(job->buffer);

    int pages = 1;
    if (numpagesrxmatch.hasMatch())
        pages = numpagesrxmatch.captured(1).toInt();

    qDebug() << "pages:" << pages;

    const int matchesPerPage = 36;

    auto lambda = [&](QSharedPointer<DownloadJobBase> job) {
        auto sjob = static_cast<DownloadStringJob *>(job.get());
        int matches = 0;
        for (auto &match : getAllRxMatches(mangarx, sjob->buffer))
        {
            mangas.links.append(match.captured(1));
            mangas.titles.append(htmlToPlainText(htmlToPlainText(match.captured(2))));
            matches++;
        }
        mangas.size += matches;

        token->sendProgress(10 + 90 * (mangas.size / matchesPerPage) / pages);

        qDebug() << "matches:" << matches;
    };

    lambda(job);

    QList<QString> urls;
    for (int i = 2; i <= pages; i++)
        urls.append(dicturl + QString::number(i));

    DownloadQueue queue(downloadManager, urls, CONF.parallelDownloadsHigh, lambda, true);
    queue.setCancellationToken(&token->canceled);
    queue.start();
    if (!queue.awaitCompletion())
    {
        token->sendError(queue.lastErrorMessage);
        return false;
    }
    this->mangaList = mangas;

    qDebug() << "mangas:" << mangas.size << "time:" << timer.elapsed();

    token->sendProgress(100);

    return true;
}

void MangaOwl::updateMangaInfoFinishedLoading(QSharedPointer<DownloadStringJob> job,
                                              QSharedPointer<MangaInfo> info)
{
    QRegularExpression titlerx(R"lit(<img class="lozad"[^>]*title="([^"]*)")lit");
    QRegularExpression authorrx(R"(Author.*?<a[^>]*>\s*(.*?)\s*</a>)",
                                QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression artistrx;
    QRegularExpression statusrx(R"(Pub. status.*?</span>\s*(.*?)\s*</p>)",
                                QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression yearrx(R"(Released.*?</span>\s*(.*?)\s*</p>)",
                              QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression genresrx(R"(<span>Genres.*?<p>(.*?)<span>)",
                                QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression summaryrx(R"lit(Story Line.*?</span>(.*?)</div>)lit",
                                 QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression coverrx(R"lit(<img class="lozad"[^>]*data-src="([^"]*)")lit");

    QRegularExpression chapterrx(
        R"lit(<a[^>]*class="chapter-url"[^>]*href="([^"]*)"[^>]*>\s*<label>\s*(.*?)\s*</label>)lit",
        QRegularExpression::DotMatchesEverythingOption);

    fillMangaInfo(info, job->buffer, titlerx, authorrx, artistrx, statusrx, yearrx, genresrx, summaryrx,
                  coverrx);

    MangaChapterCollection newchapters;
    for (auto &chapterrxmatch : getAllRxMatches(chapterrx, job->buffer))
        newchapters.insert(0, MangaChapter(chapterrxmatch.captured(2), chapterrxmatch.captured(1)));
    info->chapters.mergeChapters(newchapters);
}

Result<QStringList, QString> MangaOwl::getPageList(const QString &chapterlink)
{
    QRegularExpression pagerx(R"lit(<img[^>]*class="owl-lazy"[^>]*data-src="([^"]*)")lit");

    auto job = downloadManager->downloadAsString(chapterlink);

    if (!job->await(7000))
        return Err(job->errorString);

    QStringList imageLinks;
    for (auto &match : getAllRxMatches(pagerx, job->buffer))
    {
        imageLinks.append(match.captured(1));
    }

    return Ok(imageLinks);
}
